#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "assets/recipes_b173.hpp"
#include "world/mcregion_nbt.hpp"
#include "world/world.hpp"
#include "world/noise_b173.hpp"
#include "world/world_features.hpp"
#include "world/world_gen_b173.hpp"
#include "render/mesher_cubes.hpp"
#include "render/mesher_models.hpp"
#include "render/sky_weather.hpp"
#include "gameplay/physics.hpp"
#include "gameplay/redstone_pistons.hpp"
#include "gameplay/combat_nether.hpp"
#include "gameplay/player.hpp"
#include "entities/entities_ai.hpp"
#include "entities/entity_renderer.hpp"
#include "ui/ui_primitives.hpp"
#include "ui/ui_hud_containers.hpp"
#include "ui/ui_menus.hpp"
#include "net/net_protocol_b173.hpp"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <unordered_set>
#include <filesystem>
#include <sstream>
#include <algorithm>

// ============================================================================
// CANONICAL VOXEL ENGINE ORCHESTRATOR & SIMULATION LOOP (SPEC-2011)
// ============================================================================
// Technical Subsystem Architecture:
// 1. Dual-Frequency Simulation & Rendering Synchronization:
//    - Real-time simulation operates on an invariant 20 TPS (Ticks Per Second)
//      discrete time step (dt_tick = 0.05 seconds = 50ms).
//    - Temporal Accumulator & Linear Frame Blending:
//        tickAccumulator = min(tickAccumulator + dt_frame, 0.25F);
//        alpha = tickAccumulator / 0.05F;
//      Kinematic positions, cranial pitch/yaw, and equipped viewmodels apply
//      continuous linear interpolation: pos = prevPos + (pos - prevPos) * alpha.
// 2. Translucent Geometry Ordering & Ocean Visibility Invariants:
//    - Pass 0 (Opaque Solids): Rendered directly with back-face culling and depth
//      writing enabled (glDepthMask = GL_TRUE).
//    - Pass 1 (Translucent Fluids & Vitreous Volumes): To eliminate depth-buffer
//      occlusion holes across water boundaries, all active translucent section
//      draw calls are harvested into a dynamic array and sorted in strict
//      back-to-front order (descending Euclidean distance from eye position).
// 3. Celestial Photometric Atmosphere & Volumetric Fog Synthesis:
//    - Diurnal solar rotation angle dictates dynamic color transitions between
//      atmospheric Rayleigh daylight (0.53, 0.72, 1.0) and deep night sky.
//    - Submerged extinction coefficients trigger specialized volumetric fog:
//        * Water: Fog extinction at (0.02, 0.02, 0.20), density = 0.10.
//        * Lava:  Fog extinction at (0.60, 0.10, 0.00), density = 1.80.
// 4. Native Dynamic Binary C-ABI Extensibility Interface:
//    - Runtime loading of native dynamic libraries (*.dll) via Win32 LoadLibraryA,
//      exposing runtime block registrations (IDs 97..255) and direct spatial hooks.
// ============================================================================

extern "C" {
    struct McBetaModAPI {
        uint32_t apiVersion;
        void (*registerCustomBlock)(uint8_t id, const char* name, uint16_t spriteIdx, float hardness, uint8_t lightEmit);
        uint16_t (*getWorldBlock)(int wx, int wy, int wz);
        void (*setWorldBlock)(int wx, int wy, int wz, uint16_t rawState);
        void (*sendChatMessage)(const char* msg);
    };
    typedef void (*McBetaModInitFn)(const McBetaModAPI* api);
}

struct GpuSectionBuffer {
    GLuint  opaqueVao   = 0;
    GLuint  opaqueVbo   = 0;
    GLsizei opaqueCount = 0;
    GLuint  transVao    = 0;
    GLuint  transVbo    = 0;
    GLsizei transCount  = 0;
    int32_t builtChunkX = 0x7FFFFFFF;
    int32_t builtChunkZ = 0x7FFFFFFF;
    bool    valid       = false;
};

struct TranslucentDrawCall {
    GLuint  vao;
    GLsizei count;
    float   rx, ry, rz;
    float   distSq;
    float   gr, gg, gb;
    float   fr, fg, fb;
};

static WorldMap*                        g_modWorldPtr = nullptr;
static BetaInventoryAndHud*             g_modHudPtr   = nullptr;
static MPSCQueue<ChunkCoord, 2048>      g_meshRequestQueue;
static MPSCQueue<SectionMeshData, 1024> g_readyMeshQueue;

static inline void enqueueBlockAndNeighborChunks(int wx, int wz) {
    enqueueBlockAndNeighbors(g_meshRequestQueue, wx, wz);
}

extern "C" {
    static void modApiRegisterBlock(uint8_t id, const char* name, uint16_t spr, float hardness, uint8_t lightEmit) {
        if (g_modWorldPtr && id >= 97) {
            g_modWorldPtr->registry.registerBlock(id, name, SHAPE_CUBE, spr, spr, spr, hardness, hardness * 3.0f, lightEmit, 15, true, true, true);
        }
    }
    static uint16_t modApiGetBlock(int wx, int wy, int wz) {
        return g_modWorldPtr ? g_modWorldPtr->getBlockAt(wx, wy, wz) : 0;
    }
    static void modApiSetBlock(int wx, int wy, int wz, uint16_t st) {
        if (g_modWorldPtr && g_modWorldPtr->setBlockAt(wx, wy, wz, st, true)) {
            enqueueBlockAndNeighborChunks(wx, wz);
        }
    }
    static void modApiSendChat(const char* msg) {
        if (g_modHudPtr && msg) g_modHudPtr->addChatMessage(msg);
    }
}

static void loadNativeModsFromFolder() {
    std::error_code ec;
    std::filesystem::create_directories("mods", ec);
    McBetaModAPI api{ 1, modApiRegisterBlock, modApiGetBlock, modApiSetBlock, modApiSendChat };

    for (const auto& entry : std::filesystem::directory_iterator("mods", ec)) {
        if (entry.path().extension() == ".dll") {
            HMODULE modDll = LoadLibraryA(entry.path().string().c_str());
            if (modDll) {
                auto initFn = reinterpret_cast<McBetaModInitFn>(GetProcAddress(modDll, "ModInit"));
                if (initFn) initFn(&api);
            }
        }
    }
}

// Photometric Celestial & Atmospheric Color Solver
static void computeBetaAtmosphereColors(const WorldMap& world, const BetaSkyWeatherSystem& skyWeather,
                                        const PlayerPhysics& player, const Vec3& lookDir,
                                        int renderDistance, bool eyesInLava, float partialTicks,
                                        Vec3& outSkyColor, Vec3& outFogColor, float& outSkyBrightness) {
    if (world.dimension == -1) {
        outSkyColor      = { 0.20f, 0.03f, 0.03f };
        outFogColor      = { 0.20f, 0.03f, 0.03f };
        outSkyBrightness = 1.0f;
        return;
    }

    float celestialAngle = BetaSkyWeatherSystem::calculateCelestialAngle(world.dayTime, partialTicks);
    float f2             = BetaSkyWeatherSystem::calculateSkyLightFactor(celestialAngle);
    float rain           = skyWeather.rainStrength;

    float baseBright = (1.0f - (1.0f - f2) * 0.8f) * (1.0f - rain * 0.35f);
    if (skyWeather.lightningFlash > 0) baseBright = 1.0f;
    outSkyBrightness = std::clamp(baseBright, 0.20f, 1.0f);

    float fogR = 0.7529412f  * (f2 * 0.94f + 0.06f);
    float fogG = 0.84705883f * (f2 * 0.94f + 0.06f);
    float fogB = 1.0f        * (f2 * 0.91f + 0.09f);

    float skyR = 0.53f * outSkyBrightness;
    float skyG = 0.72f * outSkyBrightness;
    float skyB = 1.00f * outSkyBrightness;

    if (rain > 0.0f) {
        float lum = (skyR * 0.30f + skyG * 0.59f + skyB * 0.11f) * 0.60f;
        float f4  = 1.0f - rain * 0.75f;
        skyR = skyR * f4 + lum * (1.0f - f4);
        skyG = skyG * f4 + lum * (1.0f - f4);
        skyB = skyB * f4 + lum * (1.0f - f4);

        fogR *= (1.0f - rain * 0.3f);
        fogG *= (1.0f - rain * 0.3f);
        fogB *= (1.0f - rain * 0.2f);
    }

    if (skyWeather.lightningFlash > 0) {
        skyR = fogR = 0.85f;
        skyG = fogG = 0.90f;
        skyB = fogB = 1.00f;
    }

    int distTier = (renderDistance <= 4) ? 3 : ((renderDistance <= 6) ? 2 : ((renderDistance <= 8) ? 1 : 0));
    float blendF1 = 1.0f - std::pow(0.25f, static_cast<float>(4 - distTier));
    fogR += (skyR - fogR) * blendF1;
    fogG += (skyG - fogG) * blendF1;
    fogB += (skyB - fogB) * blendF1;

    float sunCos = std::cos(celestialAngle * 6.2831853f);
    if (sunCos >= -0.4f && sunCos <= 0.4f && rain < 0.8f) {
        float sf = (sunCos / 0.4f) * 0.5f + 0.5f;
        float sAlpha = 1.0f - (1.0f - std::sin(sf * 3.1415927f)) * 0.99f;
        sAlpha = sAlpha * sAlpha * (1.0f - rain);

        float sunDirX = (std::sin(celestialAngle * 6.2831853f) < 0.0f) ? 1.0f : -1.0f;
        float lookAlign = lookDir.x * sunDirX;
        if (lookAlign > 0.0f) {
            float w = lookAlign * sAlpha;
            fogR = fogR * (1.0f - w) + (sf * 0.3f + 0.7f) * w;
            fogG = fogG * (1.0f - w) + (sf * sf * 0.7f + 0.2f) * w;
            fogB = fogB * (1.0f - w) + 0.2f * w;
        }
    }

    if (player.eyesInWater) {
        skyR = fogR = 0.02f;
        skyG = fogG = 0.02f;
        skyB = fogB = 0.20f;
    } else if (eyesInLava) {
        skyR = fogR = 0.60f;
        skyG = fogG = 0.10f;
        skyB = fogB = 0.00f;
    }

    outSkyColor = { skyR, skyG, skyB };
    outFogColor = { fogR, fogG, fogB };
}

// Master Loop Entry Point
int main() {
    auto windowPtr = std::make_unique<PlatformWindow>();
    PlatformWindow& window = *windowPtr;
    if (!window.create("Minecraft Beta 1.7.3 (C++20 Bare-Metal)", 1280, 720)) {
        return 1;
    }

    GLuint chunkShader = compileShaderProgramFromFiles("shaders/chunk.vert", "shaders/chunk.frag");
    if (!chunkShader) {
        return 1;
    }

    auto audioPtr = std::make_unique<AudioEngine>();
    AudioEngine& audio = *audioPtr;
    audio.init();

    auto workerPoolPtr = std::make_unique<WorkerPool>();
    WorkerPool& workerPool = *workerPoolPtr;
    unsigned int conc = std::thread::hardware_concurrency();
    workerPool.start((conc > 2u) ? (conc - 1u) : 2u);

    auto assetsPtr         = std::make_unique<BetaAssetManager>();
    auto recipesPtr        = std::make_unique<BetaRecipeRegistry>();
    auto worldPtr          = std::make_unique<WorldMap>();
    auto worldGenPtr       = std::make_unique<Beta173WorldGenerator>(worldPtr->worldSeed);
    auto skyWeatherPtr     = std::make_unique<BetaSkyWeatherSystem>();
    auto playerPtr         = std::make_unique<PlayerPhysics>();
    auto redstonePtr       = std::make_unique<BetaRedstonePistonEngine>();
    auto combatPtr         = std::make_unique<BetaCombatAndNetherSystem>();
    auto entitiesPtr       = std::make_unique<BetaEntityManager>();
    auto entityRendererPtr = std::make_unique<BetaEntityRenderer>();
    auto uiRendererPtr     = std::make_unique<BetaUiRenderer>();
    auto hudPtr            = std::make_unique<BetaInventoryAndHud>();
    auto menusPtr          = std::make_unique<BetaMenuSystem>();
    auto fpRendererPtr     = std::make_unique<FirstPersonAndOverlayRenderer>();

    BetaAssetManager&              assets         = *assetsPtr;
    BetaRecipeRegistry&            recipes        = *recipesPtr;
    WorldMap&                      world          = *worldPtr;
    Beta173WorldGenerator&         worldGen       = *worldGenPtr;
    BetaSkyWeatherSystem&          skyWeather     = *skyWeatherPtr;
    PlayerPhysics&                 player         = *playerPtr;
    BetaRedstonePistonEngine&      redstone       = *redstonePtr;
    BetaCombatAndNetherSystem&     combat         = *combatPtr;
    BetaEntityManager&             entities       = *entitiesPtr;
    BetaEntityRenderer&            entityRenderer = *entityRendererPtr;
    BetaUiRenderer&                uiRenderer     = *uiRendererPtr;
    BetaInventoryAndHud&           hud            = *hudPtr;
    BetaMenuSystem&                menus          = *menusPtr;
    FirstPersonAndOverlayRenderer& fpRenderer     = *fpRendererPtr;

    assets.init(&audio);
    world.audioEngine  = &audio;
    world.assetManager = &assets;

    g_modWorldPtr = &world;
    g_modHudPtr   = &hud;
    loadNativeModsFromFolder();

    skyWeather.init();
    entityRenderer.init();
    uiRenderer.init();
    fpRenderer.init();
    menus.refreshWorlds(assets);

    std::vector<GpuSectionBuffer> gpuGrid(GRID_DIM * GRID_DIM * SECTIONS_PER_COL);

    BetaGameSettings settings;
    std::string activeLevelName = "New World";
    float tickAccumulator       = 0.0f;
    float totalTimeSec          = 0.0f;
    float smoothedFps           = 60.0f;
    bool  pendingPortalLink     = false;

    auto resetRuntimeCaches = [&]() {
        workerPool.clearPending();
        entities.mobs.clear();
        entities.droppedItems.clear();
        entities.particles.clear();
        combat.projectiles.clear();
        redstone.primedTnts.clear();
        redstone.delayedEvents.clear();
        for (int i = 0; i < GRID_DIM * GRID_DIM; ++i) {
            world.grid[i].state.store(CHUNK_EMPTY, std::memory_order_release);
        }
        for (auto& gs : gpuGrid) gs.valid = false;
    };

    auto startOrLoadWorld = [&](const std::string& saveDir, const std::string& levelName, int64_t seedVal) {
        resetRuntimeCaches();
        world.saveDir     = saveDir;
        world.worldSeed   = seedVal;
        world.dimension   = 0;
        activeLevelName   = levelName;
        pendingPortalLink = false;

        BetaLevelData lvl;
        if (McRegionStorage::loadLevelDat(saveDir, lvl)) {
            activeLevelName = lvl.levelName;
            world.worldSeed = lvl.seed;
            world.dayTime   = static_cast<uint64_t>(lvl.time);
            world.dimension = lvl.dimension;
            player.x = player.prevX = static_cast<float>(lvl.playerX);
            player.y = player.prevY = static_cast<float>(lvl.playerY);
            player.z = player.prevZ = static_cast<float>(lvl.playerZ);
            player.yaw    = lvl.playerYaw;
            player.pitch  = lvl.playerPitch;
            player.health = static_cast<float>(std::max<int16_t>(1, lvl.health));
            player.vx = player.vy = player.vz = 0.0f;
            player.fallDistance = 0.0f;

            for (int i = 0; i < 36; ++i) hud.mainSlots[i].clear();
            for (int i = 0; i < 4; ++i)  hud.armorSlots[i].clear();
            for (const auto& sl : lvl.inventory) {
                if (sl.slot >= 0 && sl.slot < 36) {
                    hud.mainSlots[static_cast<int>(sl.slot)] = { static_cast<uint16_t>(sl.id), static_cast<uint16_t>(sl.damage), static_cast<uint8_t>(sl.count) };
                } else if (sl.slot >= 100 && sl.slot <= 103) {
                    hud.armorSlots[static_cast<int>(sl.slot - 100)] = { static_cast<uint16_t>(sl.id), static_cast<uint16_t>(sl.damage), static_cast<uint8_t>(sl.count) };
                }
            }
        } else {
            player.x = player.prevX = 0.5f;
            player.y = player.prevY = 74.0f;
            player.z = player.prevZ = 0.5f;
            player.yaw = player.pitch = 0.0f;
            player.vx = player.vy = player.vz = 0.0f;
            player.health = 20.0f;
            for (int i = 0; i < 36; ++i) hud.mainSlots[i].clear();
            for (int i = 0; i < 4; ++i)  hud.armorSlots[i].clear();
            lvl.levelName = activeLevelName;
            lvl.seed      = seedVal;
            McRegionStorage::saveLevelDat(saveDir, lvl);
        }

        worldGen.setSeed(world.worldSeed);
        worldGen.beginAreaGeneration((static_cast<int>(std::floor(player.x))) >> 4,
                                     (static_cast<int>(std::floor(player.z))) >> 4, 1);
    };

    auto saveCompleteWorldState = [&]() {
        world.saveAllDirtyChunks();
        BetaLevelData lvl;
        lvl.levelName   = activeLevelName;
        lvl.seed        = world.worldSeed;
        lvl.time        = static_cast<int64_t>(world.dayTime);
        lvl.dimension   = world.dimension;
        lvl.playerX     = player.x;
        lvl.playerY     = player.y;
        lvl.playerZ     = player.z;
        lvl.playerYaw   = player.yaw;
        lvl.playerPitch = player.pitch;
        lvl.health      = static_cast<int16_t>(player.health);
        lvl.raining     = (skyWeather.weather != WEATHER_CLEAR);
        lvl.thundering  = (skyWeather.weather == WEATHER_THUNDER);

        for (int i = 0; i < 36; ++i) {
            if (!hud.mainSlots[i].isEmpty()) {
                lvl.inventory.push_back({ static_cast<int8_t>(i), static_cast<int16_t>(hud.mainSlots[i].id), static_cast<int8_t>(hud.mainSlots[i].count), static_cast<int16_t>(hud.mainSlots[i].damage) });
            }
        }
        for (int a = 0; a < 4; ++a) {
            if (!hud.armorSlots[a].isEmpty()) {
                lvl.inventory.push_back({ static_cast<int8_t>(100 + a), static_cast<int16_t>(hud.armorSlots[a].id), static_cast<int8_t>(hud.armorSlots[a].count), static_cast<int16_t>(hud.armorSlots[a].damage) });
            }
        }
        McRegionStorage::saveLevelDat(world.saveDir, lvl);
    };

    while (window.running) {
        float dt = window.pumpEvents();
        totalTimeSec += dt;
        smoothedFps = smoothedFps * 0.92f + (1.0f / std::max(0.0005f, dt)) * 0.08f;
        world.renderDistance = settings.renderDistance;

        // 1. World Loading & Dimensional Transition Linking
        if (menus.state == MENU_LOADING) {
            window.setMouseLock(false);
            if (worldGen.stepAreaGeneration(world, workerPool, g_meshRequestQueue, menus.loadingProg)) {
                if (pendingPortalLink) {
                    double ex = player.x, ey = player.y, ez = player.z;
                    if (BetaCombatAndNetherSystem::findExistingPortal(world, player.x, player.y, player.z, ex, ey, ez)) {
                        player.x = player.prevX = static_cast<float>(ex);
                        player.y = player.prevY = static_cast<float>(ey);
                        player.z = player.prevZ = static_cast<float>(ez);
                    } else {
                        int px = static_cast<int>(std::floor(player.x));
                        int py = static_cast<int>(std::floor(player.y));
                        int pz = static_cast<int>(std::floor(player.z));
                        BetaCombatAndNetherSystem::buildArrivalPortal(world, px, py, pz);
                        if (BetaCombatAndNetherSystem::findExistingPortal(world, player.x, player.y, player.z, ex, ey, ez)) {
                            player.x = player.prevX = static_cast<float>(ex);
                            player.y = player.prevY = static_cast<float>(ey);
                            player.z = player.prevZ = static_cast<float>(ez);
                        }
                        enqueueBlockAndNeighborChunks(px, pz);
                    }
                    pendingPortalLink = false;
                } else {
                    int topY = world.getTopSolidY(static_cast<int>(std::floor(player.x)), static_cast<int>(std::floor(player.z)));
                    if (player.y < static_cast<float>(topY) + 0.5f) {
                        player.y = player.prevY = static_cast<float>(topY) + 1.5f;
                    }
                }
                menus.state      = MENU_IN_GAME;
                hud.activeScreen = GUI_NONE;
                window.setMouseLock(true);
            }
        }

        // 2. Interactive Input & Command Dispatch
        if (menus.state == MENU_IN_GAME) {
            if (window.keysPressed[VK_F3]) hud.showDebugF3 = !hud.showDebugF3;
            if (window.keysPressed[VK_F4]) player.flying = !player.flying;

            if (player.health <= 0.0f) {
                hud.activeScreen = GUI_DEATH;
                window.setMouseLock(false);
            } else if (window.keysPressed[VK_ESCAPE]) {
                if (hud.activeScreen != GUI_NONE) {
                    hud.activeScreen = GUI_NONE;
                    window.setMouseLock(true);
                } else {
                    hud.activeScreen = GUI_PAUSE;
                    window.setMouseLock(false);
                }
            } else if (hud.activeScreen == GUI_CHAT) {
                if (window.keysPressed[VK_BACK] && !hud.chatInput.empty()) hud.chatInput.pop_back();
                for (char c : window.typedChars) {
                    if (hud.chatInput.size() < 100) hud.chatInput.push_back(c);
                }
                if (window.keysPressed[VK_RETURN]) {
                    if (!hud.chatInput.empty()) {
                        if (hud.chatInput[0] == '/') executeSlashCommand(hud.chatInput, world, player, hud, skyWeather, entities, assets);
                        else hud.addChatMessage("<Player> " + hud.chatInput);
                    }
                    hud.chatInput.clear();
                    hud.activeScreen = GUI_NONE;
                    window.setMouseLock(true);
                }
            } else {
                if (window.keysPressed['E'] || window.keysPressed['I']) {
                    if (hud.activeScreen == GUI_NONE) {
                        hud.activeScreen = GUI_INVENTORY;
                        hud.updateCraftingPreview(recipes);
                        window.setMouseLock(false);
                    } else {
                        hud.activeScreen = GUI_NONE;
                        window.setMouseLock(true);
                    }
                }
                if (window.keysPressed['T'] || window.keysPressed[VK_OEM_2]) {
                    hud.activeScreen = GUI_CHAT;
                    hud.chatInput = window.keysPressed[VK_OEM_2] ? "/" : "";
                    window.setMouseLock(false);
                }
                if (window.keysPressed['O'] && hud.activeScreen != GUI_NONE) {
                    hud.tmiVisible = !hud.tmiVisible;
                }
                if (window.keysPressed['Q'] && hud.activeScreen == GUI_NONE) {
                    ItemStack& held = hud.getHeldStack();
                    if (!held.isEmpty()) {
                        float yRad = player.yaw * 0.0174532925f;
                        float pRad = player.pitch * 0.0174532925f;
                        entities.spawnDroppedItem(held.id, held.damage, 1,
                                                  player.x + std::cos(pRad) * std::cos(yRad) * 0.8f,
                                                  player.y + 1.3f,
                                                  player.z + std::cos(pRad) * std::sin(yRad) * 0.8f);
                        if (--held.count == 0) held.clear();
                    }
                }
            }

            if (hud.activeScreen == GUI_NONE) {
                float sens = settings.mouseSensitivity * 0.22f;
                if (window.keys['C']) sens *= 0.30f;
                player.yaw   += window.rawMouseDX * sens;
                player.pitch -= window.rawMouseDY * sens;
                player.pitch  = std::clamp(player.pitch, -89.5f, 89.5f);

                if (window.wheelDelta != 0) {
                    hud.selectedHotbar = (hud.selectedHotbar - window.wheelDelta % 9 + 9) % 9;
                }
                for (int k = 0; k < 9; ++k) {
                    if (window.keysPressed['1' + k]) hud.selectedHotbar = k;
                }
            }

            // 3. Discrete 20 TPS Physics & Cellular Simulation
            if (hud.activeScreen != GUI_PAUSE && hud.activeScreen != GUI_DEATH) {
                tickAccumulator = std::min(tickAccumulator + dt, 0.25f);
                while (tickAccumulator >= 0.05f) {
                    tickAccumulator -= 0.05f;

                    float fwd = 0.0f, str = 0.0f;
                    bool jmp = false;
                    if (hud.activeScreen == GUI_NONE) {
                        if (window.keys['W']) fwd += 1.0f;
                        if (window.keys['S']) fwd -= 1.0f;
                        if (window.keys['D']) str += 1.0f;
                        if (window.keys['A']) str -= 1.0f;
                        jmp = window.keys[VK_SPACE];
                        player.sneaking = window.keys[VK_SHIFT];
                    }

                    fpRenderer.tickEquippedItem(player, hud.selectedHotbar, hud.getHeldStack());

                    float hpBeforeTick   = player.health;
                    bool bypassArmorTick = player.eyesInWater || (player.y < -64.0f);

                    player.tick(world, fwd, str, jmp, &audio, &assets);
                    world.tickSimulation(g_meshRequestQueue, &audio, &assets);
                    redstone.tick(world, g_meshRequestQueue, &audio, &assets);
                    skyWeather.tickWeather();
                    hud.tickChat();

                    for (const auto& dr : world.pendingDetachedDrops) {
                        entities.spawnDroppedItem(dr.itemId, dr.meta, dr.count, dr.x, dr.y, dr.z);
                    }
                    world.pendingDetachedDrops.clear();

                    entities.tickAll(world, player, combat, redstone, settings.difficulty,
                        [&](uint16_t id, uint16_t dmg, uint8_t cnt) { return hud.giveItem(assets, id, dmg, cnt); },
                        g_meshRequestQueue, &audio, &assets);

                    if (player.health < hpBeforeTick && !bypassArmorTick && player.fallDistance == 0.0f) {
                        float rawDmg = hpBeforeTick - player.health;
                        float reducedDmg = BetaCombatAndNetherSystem::applyBetaArmorReduction(rawDmg, hud.armorSlots, assets);
                        player.health = std::max(0.0f, hpBeforeTick - reducedDmg);
                        BetaCombatAndNetherSystem::damageArmorDurability(hud.armorSlots, assets, rawDmg);
                    }

                    int32_t targetDim = world.dimension;
                    double tx = player.x, ty = player.y, tz = player.z;
                    if (combat.tickNetherPortalTransport(world, player, targetDim, tx, ty, tz)) {
                        saveCompleteWorldState();
                        resetRuntimeCaches();

                        world.dimension = targetDim;
                        player.x = player.prevX = static_cast<float>(tx);
                        player.y = player.prevY = static_cast<float>(ty);
                        player.z = player.prevZ = static_cast<float>(tz);
                        player.vx = player.vy = player.vz = 0.0f;

                        pendingPortalLink = true;
                        worldGen.beginAreaGeneration((static_cast<int>(std::floor(player.x))) >> 4,
                                                     (static_cast<int>(std::floor(player.z))) >> 4, 1);
                        menus.state = MENU_LOADING;
                        break;
                    }

                    worldGen.updateInfiniteStreaming(world, workerPool, player.x, player.z, g_meshRequestQueue);
                }
            }
        }

        // 4. Mesh Processing Dispatch
        {
            std::unordered_set<uint64_t> seenChunks;
            ChunkCoord req;
            while (g_meshRequestQueue.pop(req)) {
                uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(req.cx)) << 32) | static_cast<uint32_t>(req.cz);
                if (!seenChunks.insert(key).second) continue;

                const ChunkColumn* col = world.getReadyChunk(req.cx, req.cz);
                if (!col) continue;

                bool smooth = settings.smoothLighting;
                int32_t rcx = req.cx, rcz = req.cz;
                workerPool.submit([&world, rcx, rcz, smooth]() {
                    const ChunkColumn* c = world.getReadyChunk(rcx, rcz);
                    if (!c) return;
                    for (int sy = 0; sy < SECTIONS_PER_COL; ++sy) {
                        SectionMeshData md{ rcx, sy, rcz, {}, {} };
                        if (c->sectionMask & (1 << sy)) {
                            BetaMesher::buildSectionMesh(world, rcx, sy, rcz, md.opaqueVertices, md.translucentVertices, smooth);
                        }
                        g_readyMeshQueue.push(md);
                    }
                }, true);
            }
        }

        // 5. Asynchronous GPU VBO Commit
        int gpuUploads = 0;
        SectionMeshData ready;
        while (gpuUploads < 12 && g_readyMeshQueue.pop(ready)) {
            int slotIdx = (((ready.chunkZ & GRID_MASK) * GRID_DIM + (ready.chunkX & GRID_MASK)) * SECTIONS_PER_COL) + ready.secY;
            GpuSectionBuffer& gs = gpuGrid[static_cast<size_t>(slotIdx)];
            gs.builtChunkX = ready.chunkX;
            gs.builtChunkZ = ready.chunkZ;
            gs.valid       = true;

            gs.opaqueCount = static_cast<GLsizei>(ready.opaqueVertices.size());
            if (gs.opaqueCount > 0) {
                if (!gs.opaqueVao) { glGenVertexArrays(1, &gs.opaqueVao); glGenBuffers(1, &gs.opaqueVbo); }
                glBindVertexArray(gs.opaqueVao);
                glBindBuffer(GL_ARRAY_BUFFER, gs.opaqueVbo);
                glBufferData(GL_ARRAY_BUFFER, gs.opaqueCount * sizeof(uint64_t), ready.opaqueVertices.data(), GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribIPointer(0, 2, GL_UNSIGNED_INT, sizeof(uint64_t), (void*)0);
            }

            gs.transCount = static_cast<GLsizei>(ready.translucentVertices.size());
            if (gs.transCount > 0) {
                if (!gs.transVao) { glGenVertexArrays(1, &gs.transVao); glGenBuffers(1, &gs.transVbo); }
                glBindVertexArray(gs.transVao);
                glBindBuffer(GL_ARRAY_BUFFER, gs.transVbo);
                glBufferData(GL_ARRAY_BUFFER, gs.transCount * sizeof(uint64_t), ready.translucentVertices.data(), GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribIPointer(0, 2, GL_UNSIGNED_INT, sizeof(uint64_t), (void*)0);
            }
            gpuUploads++;
        }

        // 6. Volumetric World Render Pipeline
        float aspect = static_cast<float>(window.width) / static_cast<float>(std::max(1, window.height));
        float alpha  = tickAccumulator / 0.05f;

        Vec3 eyePos;
        player.getInterpolatedEyePos(alpha, eyePos.x, eyePos.y, eyePos.z);

        float yRad = player.yaw * 0.0174532925f;
        float pRad = player.pitch * 0.0174532925f;
        Vec3 lookDir{ std::cos(pRad) * std::cos(yRad), std::sin(pRad), std::cos(pRad) * std::sin(yRad) };

        uint8_t eyeBlkId = static_cast<uint8_t>((world.getBlockAt(
            static_cast<int>(std::floor(eyePos.x)),
            static_cast<int>(std::floor(eyePos.y)),
            static_cast<int>(std::floor(eyePos.z))) >> 4) & 0xFF);
        bool eyesInLava = (eyeBlkId == 10 || eyeBlkId == 11);

        Vec3 skyCol{ 0.53f, 0.72f, 1.0f };
        Vec3 fogCol{ 0.75f, 0.85f, 1.0f };
        float effectiveSkyBright = world.getSkyBrightness();
        computeBetaAtmosphereColors(world, skyWeather, player, lookDir, settings.renderDistance, eyesInLava, alpha,
                                    skyCol, fogCol, effectiveSkyBright);

        glClearColor(fogCol.x, fogCol.y, fogCol.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (menus.state == MENU_IN_GAME) {
            float baseFov = player.eyesInWater ? 60.0f : 70.0f;
            float fov     = (window.keys['C'] && hud.activeScreen == GUI_NONE) ? 22.0f : baseFov;
            float farDist = static_cast<float>(settings.renderDistance) * 16.0f;

            const float PI = 3.14159265f;
            float wd       = player.prevWalkDist + (player.walkDist - player.prevWalkDist) * alpha;
            float bob      = player.prevCameraYaw + (player.cameraYaw - player.prevCameraYaw) * alpha;
            float bobRoll  = std::sin(wd * PI) * bob * 3.0f;
            float bobPitch = std::abs(std::cos(wd * PI - 0.2f) * bob) * 5.0f;

            float hurtRoll = 0.0f;
            if (player.hurtTime > 0) {
                float ht = (static_cast<float>(player.hurtTime) - alpha) / 10.0f;
                hurtRoll = std::sin(ht * ht * PI) * 14.0f;
            }

            Mat4 proj = Mat4::perspective(fov, aspect, 0.05f, farDist + 64.0f) *
                        Mat4::rotateZ(bobRoll + hurtRoll) *
                        Mat4::rotateX(bobPitch);
            Mat4 view = Mat4::lookAt({0.0f, 0.0f, 0.0f}, lookDir, {0.0f, 1.0f, 0.0f});
            Mat4 viewProj = proj * view;
            Frustum frustum;
            frustum.extract(viewProj);

            RaycastHit hit = PlayerPhysics::raycast(world, eyePos.x, eyePos.y, eyePos.z, lookDir.x, lookDir.y, lookDir.z, 4.5f);
            if (hud.activeScreen == GUI_NONE) {
                BetaInteractionController::processPrimaryAction(
                    world, assets, player, hud, entities, combat, redstone, audio,
                    eyePos, lookDir, hit, window.leftClicked, window.leftDown, dt, g_meshRequestQueue);

                if (window.rightClicked) {
                    BetaInteractionController::processSecondaryAction(
                        world, assets, recipes, window, player, hud, entities, combat, redstone, audio,
                        eyePos, lookDir, hit, g_meshRequestQueue);
                }
            }

            // 6A. Celestial Dome
            skyWeather.renderSky(world, assets, proj, view);

            // 6B. Terrain Meshes (Pass 0: Opaque Solids | Pass 1: Back-to-Front Sorted Fluids & Translucents)
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glUseProgram(chunkShader);
            glUniformMatrix4fv(glGetUniformLocation(chunkShader, "u_viewProj"), 1, GL_FALSE, viewProj.m);
            glUniform1f(glGetUniformLocation(chunkShader, "u_skyBrightness"), effectiveSkyBright);
            glUniform1f(glGetUniformLocation(chunkShader, "u_torchFlicker"), std::sin(totalTimeSec * 12.0f) * 0.015f);
            glUniform3f(glGetUniformLocation(chunkShader, "u_fogColor"), fogCol.x, fogCol.y, fogCol.z);

            float fogStartDist = (world.dimension == -1) ? (farDist * 0.25f) : (farDist * 0.55f);
            bool expFog        = player.eyesInWater || eyesInLava;
            float expDensity   = eyesInLava ? 1.80f : 0.10f;
            glUniform1f(glGetUniformLocation(chunkShader, "u_fogStart"), fogStartDist);
            glUniform1f(glGetUniformLocation(chunkShader, "u_fogEnd"), farDist);
            glUniform1i(glGetUniformLocation(chunkShader, "u_fogMode"), expFog ? 1 : 0);
            glUniform1f(glGetUniformLocation(chunkShader, "u_fogDensity"), expDensity);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, assets.terrainTex);
            glUniform1i(glGetUniformLocation(chunkShader, "u_terrainTex"), 0);

            GLint uOffset  = glGetUniformLocation(chunkShader, "u_chunkOffset");
            GLint uGrass   = glGetUniformLocation(chunkShader, "u_grassColor");
            GLint uFoliage = glGetUniformLocation(chunkShader, "u_foliageColor");

            int pcx = (static_cast<int>(std::floor(eyePos.x))) >> 4;
            int pcz = (static_cast<int>(std::floor(eyePos.z))) >> 4;
            int rd  = settings.renderDistance;

            std::vector<TranslucentDrawCall> transDrawList;
            transDrawList.reserve(128);

            // --- PASS 0: OPAQUE PASS (Front-to-back or natural order with depth-write) ---
            glDepthMask(GL_TRUE);
            for (int dz = -rd; dz <= rd; ++dz) {
                for (int dx = -rd; dx <= rd; ++dx) {
                    int32_t cx = pcx + dx, cz = pcz + dz;
                    const ChunkColumn* col = world.getReadyChunk(cx, cz);
                    if (!col) continue;

                    float gr, gg, gb, fr, fg, fb;
                    assets.sampleBiomeColor(col->temperature[136], col->humidity[136], false, gr, gg, gb);
                    assets.sampleBiomeColor(col->temperature[136], col->humidity[136], true,  fr, fg, fb);
                    glUniform3f(uGrass,   gr, gg, gb);
                    glUniform3f(uFoliage, fr, fg, fb);

                    for (int sy = 0; sy < SECTIONS_PER_COL; ++sy) {
                        int slotIdx = (((cz & GRID_MASK) * GRID_DIM + (cx & GRID_MASK)) * SECTIONS_PER_COL) + sy;
                        const GpuSectionBuffer& gs = gpuGrid[static_cast<size_t>(slotIdx)];
                        if (!gs.valid || gs.builtChunkX != cx || gs.builtChunkZ != cz) continue;

                        float rx = static_cast<float>(cx * 16) - eyePos.x;
                        float ry = static_cast<float>(sy * 16) - eyePos.y;
                        float rz = static_cast<float>(cz * 16) - eyePos.z;
                        if (!frustum.isBoxVisible(rx, ry, rz, rx + 16.0f, ry + 16.0f, rz + 16.0f)) continue;

                        if (gs.opaqueCount > 0) {
                            glUniform3f(uOffset, rx, ry, rz);
                            glBindVertexArray(gs.opaqueVao);
                            glDrawArrays(GL_TRIANGLES, 0, gs.opaqueCount);
                        }

                        // Harvest visible translucent sections for sorted Pass 1
                        if (gs.transCount > 0) {
                            float midX = rx + 8.0f, midY = ry + 8.0f, midZ = rz + 8.0f;
                            float dSq = midX * midX + midY * midY + midZ * midZ;
                            transDrawList.push_back({ gs.transVao, gs.transCount, rx, ry, rz, dSq, gr, gg, gb, fr, fg, fb });
                        }
                    }
                }
            }

            // --- PASS 1: TRANSLUCENT PASS (Strict Back-to-Front Distance Sorting, Eliminates Ocean Holes) ---
            if (!transDrawList.empty()) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -1.0f);

                std::sort(transDrawList.begin(), transDrawList.end(), [](const TranslucentDrawCall& a, const TranslucentDrawCall& b) {
                    return a.distSq > b.distSq; // Farthest chunks render first
                });

                for (const auto& dc : transDrawList) {
                    glUniform3f(uGrass,   dc.gr, dc.gg, dc.gb);
                    glUniform3f(uFoliage, dc.fr, dc.fg, dc.fb);
                    glUniform3f(uOffset,  dc.rx, dc.ry, dc.rz);
                    glBindVertexArray(dc.vao);
                    glDrawArrays(GL_TRIANGLES, 0, dc.count);
                }

                glDisable(GL_POLYGON_OFFSET_FILL);
                glDisable(GL_BLEND);
            }

            // 6C. Entities, Selection Wireframe, Breaking Cracks, Atmosphere & First-Person Overlays
            entityRenderer.renderAll(world, assets, entities, combat, redstone, viewProj, eyePos, alpha, totalTimeSec);
            if (hit.hit && hud.activeScreen == GUI_NONE) {
                fpRenderer.drawSelectionBox(world, viewProj, eyePos, hit.bx, hit.by, hit.bz);
                if (player.breakProgress > 0.0f) {
                    fpRenderer.drawBlockBreakCracks(viewProj, assets, eyePos, hit.bx, hit.by, hit.bz, player.breakProgress);
                }
            }
            skyWeather.renderClouds(world, assets, viewProj, eyePos);
            skyWeather.renderWeather(world, assets, viewProj, eyePos);

            if (hud.activeScreen == GUI_NONE) {
                fpRenderer.drawFirstPersonHeld(world, assets, player, hud.getHeldStack(), aspect, alpha, effectiveSkyBright);
            }
            if (player.fireTicks > 0 && !player.flying) {
                fpRenderer.drawFirstPersonFireOverlay(assets, totalTimeSec);
            }
        }

        // 7. Raster 2D UI & Diagnostic Hud
        uiRenderer.beginFrame(window.width, window.height, settings.guiScale);
        int mx = window.mouseX / uiRenderer.guiScale;
        int my = window.mouseY / uiRenderer.guiScale;

        if (menus.state == MENU_IN_GAME) {
            hud.renderHUD(uiRenderer, world, assets, player, entities, smoothedFps);
            hud.renderContainerScreen(uiRenderer, world, assets, recipes, mx, my, window.leftClicked, window.rightClicked);

            if (hud.activeScreen == GUI_PAUSE) {
                uiRenderer.drawGradientRect(0, 0, static_cast<float>(uiRenderer.scaledW), static_cast<float>(uiRenderer.scaledH), 0xC0101010, 0xD0101010);
                float cx = uiRenderer.scaledW * 0.5f, cy = uiRenderer.scaledH * 0.5f;
                uiRenderer.drawCenteredString(assets, "Game menu", cx, cy - 56.0f, 0xFFFFFFFF);
                if (uiRenderer.drawClassicButton(assets, "Back to Game", cx - 100.0f, cy - 32.0f, 200.0f, 20.0f, mx, my, window.leftClicked)) {
                    hud.activeScreen = GUI_NONE;
                    window.setMouseLock(true);
                }
                if (uiRenderer.drawClassicButton(assets, "Options...", cx - 100.0f, cy - 8.0f, 200.0f, 20.0f, mx, my, window.leftClicked)) {
                    menus.returnState = MENU_IN_GAME;
                    menus.state       = MENU_OPTIONS;
                }
                if (uiRenderer.drawClassicButton(assets, "Save and quit to title", cx - 100.0f, cy + 16.0f, 200.0f, 20.0f, mx, my, window.leftClicked)) {
                    saveCompleteWorldState();
                    hud.activeScreen = GUI_NONE;
                    menus.state      = MENU_TITLE;
                }
            } else if (hud.activeScreen == GUI_DEATH) {
                uiRenderer.drawGradientRect(0, 0, static_cast<float>(uiRenderer.scaledW), static_cast<float>(uiRenderer.scaledH), 0x60500000, 0x80300000);
                float cx = uiRenderer.scaledW * 0.5f, cy = uiRenderer.scaledH * 0.5f;
                uiRenderer.drawCenteredString(assets, "Game over!", cx, cy - 32.0f, 0xFFFFFFFF);
                if (uiRenderer.drawClassicButton(assets, "Respawn", cx - 100.0f, cy, 200.0f, 20.0f, mx, my, window.leftClicked)) {
                    player.x = player.prevX = 0.5f;
                    player.z = player.prevZ = 0.5f;
                    player.y = player.prevY = static_cast<float>(world.getTopSolidY(0, 0)) + 1.5f;
                    player.vx = player.vy = player.vz = 0.0f;
                    player.fallDistance = 0.0f;
                    player.fireTicks    = 0;
                    player.airSupply    = 300;
                    player.health       = 20.0f;
                    hud.activeScreen    = GUI_NONE;
                    window.setMouseLock(true);
                }
                if (uiRenderer.drawClassicButton(assets, "Title menu", cx - 100.0f, cy + 24.0f, 200.0f, 20.0f, mx, my, window.leftClicked)) {
                    saveCompleteWorldState();
                    hud.activeScreen = GUI_NONE;
                    menus.state      = MENU_TITLE;
                }
            }
        } else {
            bool quitReq = false;
            std::string targetSaveDir, targetWorldName;
            int64_t targetSeed = 0;
            if (menus.renderMenus(uiRenderer, assets, settings, window.typedChars, window.keysPressed[VK_BACK],
                                  mx, my, window.leftClicked, quitReq, targetSaveDir, targetWorldName, targetSeed)) {
                startOrLoadWorld(targetSaveDir, targetWorldName, targetSeed);
            }
            if (quitReq) window.running = false;
        }

        uiRenderer.endFrame();
        window.swapBuffers();
    }

    if (menus.state == MENU_IN_GAME) {
        saveCompleteWorldState();
    }
    workerPool.shutdown();
    audio.shutdown();
    return 0;
}