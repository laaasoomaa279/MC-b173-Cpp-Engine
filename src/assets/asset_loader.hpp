#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "miniz.h"
#include "stb_image.h"
#include "stb_vorbis.c"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

// ============================================================================
// Minecraft Beta 1.7.3 Complete Asset Loader (1:1 Java Texture & Sound Fidelity)
// ============================================================================

enum Beta173ItemId : uint16_t {
    ITEM_IRON_SHOVEL       = 256,
    ITEM_IRON_PICKAXE      = 257,
    ITEM_IRON_AXE          = 258,
    ITEM_FLINT_AND_STEEL   = 259,
    ITEM_APPLE             = 260,
    ITEM_BOW               = 261,
    ITEM_ARROW             = 262,
    ITEM_COAL              = 263,
    ITEM_DIAMOND           = 264,
    ITEM_IRON_INGOT        = 265,
    ITEM_GOLD_INGOT        = 266,
    ITEM_IRON_SWORD        = 267,
    ITEM_WOOD_SWORD        = 268,
    ITEM_WOOD_SHOVEL       = 269,
    ITEM_WOOD_PICKAXE      = 270,
    ITEM_WOOD_AXE          = 271,
    ITEM_STONE_SWORD       = 272,
    ITEM_STONE_SHOVEL      = 273,
    ITEM_STONE_PICKAXE     = 274,
    ITEM_STONE_AXE         = 275,
    ITEM_DIAMOND_SWORD     = 276,
    ITEM_DIAMOND_SHOVEL    = 277,
    ITEM_DIAMOND_PICKAXE   = 278,
    ITEM_DIAMOND_AXE       = 279,
    ITEM_STICK             = 280,
    ITEM_BOWL              = 281,
    ITEM_MUSHROOM_STEW     = 282,
    ITEM_GOLD_SWORD        = 283,
    ITEM_GOLD_SHOVEL       = 284,
    ITEM_GOLD_PICKAXE      = 285,
    ITEM_GOLD_AXE          = 286,
    ITEM_STRING            = 287,
    ITEM_FEATHER           = 288,
    ITEM_GUNPOWDER         = 289,
    ITEM_WOOD_HOE          = 290,
    ITEM_STONE_HOE         = 291,
    ITEM_IRON_HOE          = 292,
    ITEM_DIAMOND_HOE       = 293,
    ITEM_GOLD_HOE          = 294,
    ITEM_SEEDS             = 295,
    ITEM_WHEAT             = 296,
    ITEM_BREAD             = 297,
    ITEM_LEATHER_HELMET    = 298,
    ITEM_LEATHER_CHEST     = 299,
    ITEM_LEATHER_LEGS      = 300,
    ITEM_LEATHER_BOOTS     = 301,
    ITEM_CHAIN_HELMET      = 302,
    ITEM_CHAIN_CHEST       = 303,
    ITEM_CHAIN_LEGS        = 304,
    ITEM_CHAIN_BOOTS       = 305,
    ITEM_IRON_HELMET       = 306,
    ITEM_IRON_CHEST        = 307,
    ITEM_IRON_LEGS         = 308,
    ITEM_IRON_BOOTS        = 309,
    ITEM_DIAMOND_HELMET    = 310,
    ITEM_DIAMOND_CHEST     = 311,
    ITEM_DIAMOND_LEGS      = 312,
    ITEM_DIAMOND_BOOTS     = 313,
    ITEM_GOLD_HELMET       = 314,
    ITEM_GOLD_CHEST        = 315,
    ITEM_GOLD_LEGS         = 316,
    ITEM_GOLD_BOOTS        = 317,
    ITEM_FLINT             = 318,
    ITEM_PORKCHOP_RAW      = 319,
    ITEM_PORKCHOP_COOKED   = 320,
    ITEM_PAINTING          = 321,
    ITEM_GOLDEN_APPLE      = 322,
    ITEM_SIGN              = 323,
    ITEM_WOOD_DOOR         = 324,
    ITEM_BUCKET            = 325,
    ITEM_WATER_BUCKET      = 326,
    ITEM_LAVA_BUCKET       = 327,
    ITEM_MINECART          = 328,
    ITEM_SADDLE            = 329,
    ITEM_IRON_DOOR         = 330,
    ITEM_REDSTONE          = 331,
    ITEM_SNOWBALL          = 332,
    ITEM_BOAT              = 333,
    ITEM_LEATHER           = 334,
    ITEM_MILK_BUCKET       = 335,
    ITEM_BRICK_INGOT       = 336,
    ITEM_CLAY_BALL         = 337,
    ITEM_REEDS             = 338,
    ITEM_PAPER             = 339,
    ITEM_BOOK              = 340,
    ITEM_SLIME_BALL        = 341,
    ITEM_CHEST_MINECART    = 342,
    ITEM_FURNACE_MINECART  = 343,
    ITEM_EGG               = 344,
    ITEM_COMPASS           = 345,
    ITEM_FISHING_ROD       = 346,
    ITEM_CLOCK             = 347,
    ITEM_GLOWSTONE_DUST    = 348,
    ITEM_FISH_RAW          = 349,
    ITEM_FISH_COOKED       = 350,
    ITEM_DYE               = 351,
    ITEM_BONE              = 352,
    ITEM_SUGAR             = 353,
    ITEM_CAKE_ITEM         = 354,
    ITEM_BED_ITEM          = 355,
    ITEM_REPEATER_ITEM     = 356,
    ITEM_COOKIE            = 357,
    ITEM_MAP               = 358,
    ITEM_SHEARS            = 359,
    ITEM_RECORD_13         = 2256,
    ITEM_RECORD_CAT        = 2257
};

enum ArmorSlotType : uint8_t {
    ARMOR_NONE   = 0,
    ARMOR_HELMET = 1,
    ARMOR_CHEST  = 2,
    ARMOR_LEGS   = 3,
    ARMOR_BOOTS  = 4
};

struct ItemDef {
    const char*   name         = "Unknown";
    uint16_t      spriteIdx    = 0;
    uint8_t       maxStackSize = 64;
    uint16_t      maxDamage    = 0;
    ToolCategory  toolType     = TOOL_NONE;
    uint8_t       harvestLevel = 0;
    float         miningSpeed  = 1.0f;
    float         attackDamage = 1.0f;
    int           healAmount   = 0;
    ArmorSlotType armorSlot    = ARMOR_NONE;
    int           armorPoints  = 0;
    uint8_t       placesBlock  = 0;
};

enum MobSkinId : uint8_t {
    SKIN_PLAYER    = 0,
    SKIN_ZOMBIE    = 1,
    SKIN_CREEPER   = 2,
    SKIN_SKELETON  = 3,
    SKIN_SPIDER    = 4,
    SKIN_PIG       = 5,
    SKIN_SHEEP     = 6,
    SKIN_SHEEP_FUR = 7,
    SKIN_COW       = 8,
    SKIN_GHAST     = 9,
    SKIN_COUNT     = 10
};

class BetaAssetManager {
public:
    std::string jarPath;
    std::string mcRootDir;

    // Core OpenGL Textures
    GLuint terrainTex     = 0; // terrain.png (256x256)
    GLuint itemsTex       = 0; // gui/items.png (256x256)
    GLuint fontTex        = 0; // font/default.png
    GLuint mcLogoTex      = 0; // title/mclogo.png
    GLuint guiTex         = 0; // gui/gui.png
    GLuint iconsTex       = 0; // gui/icons.png
    GLuint inventoryTex   = 0; // gui/inventory.png
    GLuint craftingTex    = 0; // gui/crafting.png
    GLuint furnaceTex     = 0; // gui/furnace.png
    GLuint containerTex   = 0; // gui/container.png
    GLuint sunTex         = 0; // terrain/sun.png
    GLuint moonTex        = 0; // terrain/moon.png
    GLuint cloudsTex      = 0; // environment/clouds.png
    GLuint rainTex        = 0; // environment/rain.png
    GLuint snowTex        = 0; // environment/snow.png
    GLuint shadowTex      = 0; // misc/shadow.png
    GLuint mobSkins[SKIN_COUNT]{};

    uint8_t charWidths[256]{};
    std::vector<std::string> splashTexts;

    std::vector<uint8_t> grassColormap;
    std::vector<uint8_t> foliageColormap;

    ItemDef itemDefs[512];
    ItemDef record13Def;
    ItemDef recordCatDef;

    SoundBuffer digSounds[9];
    SoundBuffer stepSounds[9];
    SoundBuffer hitSound;
    SoundBuffer explodeSound;
    SoundBuffer bowSound;
    SoundBuffer popSound;
    SoundBuffer clickSound;
    SoundBuffer doorOpenSound;
    SoundBuffer doorCloseSound;
    SoundBuffer fizzSound;

    void init(AudioEngine* audio = nullptr) {
        for (int i = 0; i < 256; ++i) charWidths[i] = 6;
        charWidths[' '] = 4;

        registerAllBeta173Items();
        discoverMinecraftPaths();

        mz_zip_archive zip{};
        bool hasJar = (!jarPath.empty() && mz_zip_reader_init_file(&zip, jarPath.c_str(), 0));
        mz_zip_archive* zPtr = hasJar ? &zip : nullptr;

        terrainTex   = loadTextureFromSources(zPtr, "terrain.png");
        itemsTex     = loadTextureFromSources(zPtr, "gui/items.png");
        fontTex      = loadFontAndMeasureWidths(zPtr, "font/default.png");
        mcLogoTex    = loadTextureFromSources(zPtr, "title/mclogo.png", false, false);
        guiTex       = loadTextureFromSources(zPtr, "gui/gui.png");
        iconsTex     = loadTextureFromSources(zPtr, "gui/icons.png");
        inventoryTex = loadTextureFromSources(zPtr, "gui/inventory.png");
        craftingTex  = loadTextureFromSources(zPtr, "gui/crafting.png");
        furnaceTex   = loadTextureFromSources(zPtr, "gui/furnace.png");
        containerTex = loadTextureFromSources(zPtr, "gui/container.png");
        sunTex       = loadTextureFromSources(zPtr, "terrain/sun.png");
        moonTex      = loadTextureFromSources(zPtr, "terrain/moon.png");
        cloudsTex    = loadTextureFromSources(zPtr, "environment/clouds.png");
        rainTex      = loadTextureFromSources(zPtr, "environment/rain.png");
        snowTex      = loadTextureFromSources(zPtr, "environment/snow.png");
        shadowTex    = loadTextureFromSources(zPtr, "misc/shadow.png", true);

        mobSkins[SKIN_PLAYER]    = loadTextureFromSources(zPtr, "mob/char.png");
        mobSkins[SKIN_ZOMBIE]    = loadTextureFromSources(zPtr, "mob/zombie.png");
        mobSkins[SKIN_CREEPER]   = loadTextureFromSources(zPtr, "mob/creeper.png");
        mobSkins[SKIN_SKELETON]  = loadTextureFromSources(zPtr, "mob/skeleton.png");
        mobSkins[SKIN_SPIDER]    = loadTextureFromSources(zPtr, "mob/spider.png");
        mobSkins[SKIN_PIG]       = loadTextureFromSources(zPtr, "mob/pig.png");
        mobSkins[SKIN_SHEEP]     = loadTextureFromSources(zPtr, "mob/sheep.png");
        mobSkins[SKIN_SHEEP_FUR] = loadTextureFromSources(zPtr, "mob/sheep_fur.png");
        mobSkins[SKIN_COW]       = loadTextureFromSources(zPtr, "mob/cow.png");
        mobSkins[SKIN_GHAST]     = loadTextureFromSources(zPtr, "mob/ghast.png");

        // Replace empty Mojang "FIRE TEX! HINT" / water / lava placeholder tiles with real textures
        generateProceduralAnimatedTiles();

        loadColormap(zPtr, "misc/grasscolor.png", grassColormap);
        loadColormap(zPtr, "misc/foliagecolor.png", foliageColormap);
        loadSplashes(zPtr);

        if (audio) {
            loadClassicSounds(zPtr, *audio);
        }

        if (hasJar) {
            mz_zip_reader_end(&zip);
        }
    }

    const ItemDef& getItemDef(uint16_t id) const {
        if (id == ITEM_RECORD_13)  return record13Def;
        if (id == ITEM_RECORD_CAT) return recordCatDef;
        if (id < 512) return itemDefs[id];
        return itemDefs[0];
    }

    uint8_t getMaxStackSize(uint16_t id) const {
        if (id < 256) return 64;
        return getItemDef(id).maxStackSize;
    }

    void sampleBiomeColor(float temp, float rain, bool foliage, float& outR, float& outG, float& outB) const {
        const auto& map = foliage ? foliageColormap : grassColormap;
        if (map.size() < 256 * 256 * 4) {
            outR = foliage ? 0.35f : 0.55f;
            outG = foliage ? 0.72f : 0.85f;
            outB = foliage ? 0.22f : 0.34f;
            return;
        }
        float t = std::clamp(temp, 0.0f, 1.0f);
        float r = std::clamp(rain, 0.0f, 1.0f) * t;
        int px = std::clamp((int)((1.0f - t) * 255.0f), 0, 255);
        int py = std::clamp((int)((1.0f - r) * 255.0f), 0, 255);
        size_t idx = ((size_t)py * 256 + (size_t)px) * 4;
        outR = map[idx + 0] / 255.0f;
        outG = map[idx + 1] / 255.0f;
        outB = map[idx + 2] / 255.0f;
    }

private:
    void generateProceduralAnimatedTiles() {
        if (!terrainTex) return;
        glBindTexture(GL_TEXTURE_2D, terrainTex);

        uint8_t firePx[16 * 16 * 4]{};
        uint8_t waterPx[16 * 16 * 4]{};
        uint8_t lavaPx[16 * 16 * 4]{};

        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                int idx = (y * 16 + x) * 4;

                // Classic Beta 1.7.3 Water (16x16 blue)
                uint8_t wShade = (uint8_t)(215 + ((x * 7 + y * 13) & 31));
                waterPx[idx + 0] = 35;
                waterPx[idx + 1] = 80;
                waterPx[idx + 2] = wShade;
                waterPx[idx + 3] = 215;

                // Classic Beta 1.7.3 Lava (16x16 molten orange)
                uint8_t lShade = (uint8_t)(165 + ((x * 11 + y * 5) & 63));
                lavaPx[idx + 0] = 248;
                lavaPx[idx + 1] = lShade;
                lavaPx[idx + 2] = 20;
                lavaPx[idx + 3] = 255;

                // Classic Beta 1.7.3 Fire (eliminates red "FIRE TEX! HINT" square)
                float fx = std::abs((x - 7.5f) / 7.5f);
                float fy = (float)y / 15.0f; // 0 = top, 1 = bottom
                float flame = fy - fx * 0.65f + (((x * 3 + y * 5) & 3) * 0.08f);
                if (flame > 0.35f) {
                    firePx[idx + 0] = 255;
                    firePx[idx + 1] = (uint8_t)std::clamp((int)(flame * 235.0f), 85, 245);
                    firePx[idx + 2] = (flame > 0.85f) ? 140 : 20;
                    firePx[idx + 3] = 255;
                }
            }
        }

        // Slot 31 = Fire, Slot 205 = Water, Slot 237 = Lava
        glTexSubImage2D(GL_TEXTURE_2D, 0, (31 % 16) * 16,  (31 / 16) * 16,  16, 16, GL_RGBA, GL_UNSIGNED_BYTE, firePx);
        glTexSubImage2D(GL_TEXTURE_2D, 0, (205 % 16) * 16, (205 / 16) * 16, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, waterPx);
        glTexSubImage2D(GL_TEXTURE_2D, 0, (237 % 16) * 16, (237 / 16) * 16, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, lavaPx);
    }

    void discoverMinecraftPaths() {
        const char* appData = std::getenv("APPDATA");
        if (appData) {
            mcRootDir = std::string(appData) + "\\.minecraft";
        }
        const std::string candidates[] = {
            "b1.7.3.jar",
            "../b1.7.3.jar",
            "assets/b1.7.3.jar",
            "../assets/b1.7.3.jar",
            mcRootDir + "\\versions\\b1.7.3\\b1.7.3.jar",
            mcRootDir + "\\versions\\b1.7.3-201307241547\\b1.7.3-201307241547.jar",
            mcRootDir + "\\bin\\minecraft.jar"
        };
        for (const auto& p : candidates) {
            std::error_code ec;
            if (!p.empty() && std::filesystem::exists(p, ec)) {
                jarPath = p;
                break;
            }
        }
    }

    static bool readRawBytesFromJarOrDisk(mz_zip_archive* zip, const std::string& relPath, std::vector<uint8_t>& out) {
        const std::string prefixes[] = { "assets/", "../assets/" };
        for (const auto& pre : prefixes) {
            std::ifstream f(pre + relPath, std::ios::binary);
            if (f.is_open()) {
                f.seekg(0, std::ios::end);
                size_t sz = (size_t)f.tellg();
                f.seekg(0, std::ios::beg);
                out.resize(sz);
                f.read((char*)out.data(), sz);
                return !out.empty();
            }
        }

        if (zip) {
            size_t sz = 0;
            void* ptr = mz_zip_reader_extract_file_to_heap(zip, relPath.c_str(), &sz, 0);
            if (ptr && sz > 0) {
                out.assign((uint8_t*)ptr, (uint8_t*)ptr + sz);
                mz_free(ptr);
                return true;
            }
            if (ptr) mz_free(ptr);
        }
        return false;
    }

    static GLuint uploadRGBA(const uint8_t* pixels, int w, int h, bool linearFilter = false) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        GLint filter = linearFilter ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        return tex;
    }

    static GLuint loadTextureFromSources(mz_zip_archive* zip, const std::string& relPath,
                                         bool linearFilter = false, bool useFallbackChecker = true) {
        std::vector<uint8_t> raw;
        if (readRawBytesFromJarOrDisk(zip, relPath, raw)) {
            int w = 0, h = 0, ch = 0;
            uint8_t* px = stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &ch, 4);
            if (px) {
                GLuint id = uploadRGBA(px, w, h, linearFilter);
                stbi_image_free(px);
                return id;
            }
        }
        if (!useFallbackChecker) return 0;

        std::vector<uint8_t> fallback(256 * 256 * 4, 255);
        for (int y = 0; y < 256; ++y) {
            for (int x = 0; x < 256; ++x) {
                bool checker = ((x / 8) ^ (y / 8)) & 1;
                size_t idx = ((size_t)y * 256 + (size_t)x) * 4;
                fallback[idx + 0] = checker ? 130 : 95;
                fallback[idx + 1] = checker ? 130 : 95;
                fallback[idx + 2] = checker ? 130 : 95;
                fallback[idx + 3] = 255;
            }
        }
        return uploadRGBA(fallback.data(), 256, 256, linearFilter);
    }

    GLuint loadFontAndMeasureWidths(mz_zip_archive* zip, const std::string& relPath) {
        std::vector<uint8_t> raw;
        if (readRawBytesFromJarOrDisk(zip, relPath, raw)) {
            int w = 0, h = 0, ch = 0;
            uint8_t* px = stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &ch, 4);
            if (px) {
                int cellW = w / 16;
                int cellH = h / 16;
                for (int c = 0; c < 256; ++c) {
                    if (c == ' ') { charWidths[c] = 4; continue; }
                    int gx = (c % 16) * cellW;
                    int gy = (c / 16) * cellH;
                    int maxCol = 0;
                    for (int x = 0; x < cellW; ++x) {
                        for (int y = 0; y < cellH; ++y) {
                            uint8_t a = px[((gy + y) * w + (gx + x)) * 4 + 3];
                            if (a > 10) maxCol = std::max(maxCol, x + 1);
                        }
                    }
                    float scaleTo8 = 8.0f / std::max(1, cellW);
                    charWidths[c] = (uint8_t)std::clamp((int)std::round(maxCol * scaleTo8) + 1, 2, 9);
                }
                GLuint id = uploadRGBA(px, w, h, false);
                stbi_image_free(px);
                return id;
            }
        }
        return loadTextureFromSources(nullptr, relPath, false);
    }

    static void loadColormap(mz_zip_archive* zip, const std::string& relPath, std::vector<uint8_t>& outRGBA) {
        std::vector<uint8_t> raw;
        if (readRawBytesFromJarOrDisk(zip, relPath, raw)) {
            int w = 0, h = 0, ch = 0;
            uint8_t* px = stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &ch, 4);
            if (px && w == 256 && h == 256) {
                outRGBA.assign(px, px + 256 * 256 * 4);
                stbi_image_free(px);
                return;
            }
            if (px) stbi_image_free(px);
        }
    }

    void loadSplashes(mz_zip_archive* zip) {
        std::vector<uint8_t> raw;
        if (readRawBytesFromJarOrDisk(zip, "title/splashes.txt", raw)) {
            std::string txt((const char*)raw.data(), raw.size());
            std::istringstream ss(txt);
            std::string line;
            while (std::getline(ss, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
                if (!line.empty()) splashTexts.push_back(std::move(line));
            }
        }
        if (splashTexts.empty()) {
            splashTexts = {
                "Beta 1.7.3!",
                "Falling off cliffs!",
                "Now with Pistons!",
                "Classic Nostalgia!",
                "Punching wood since 2011!"
            };
        }
    }

    static bool decodeOggToSoundBuffer(const uint8_t* data, size_t len, AudioEngine& audio, SoundBuffer& out) {
        int channels = 0, sampleRate = 0;
        short* decoded = nullptr;
        int samples = stb_vorbis_decode_memory(data, (int)len, &channels, &sampleRate, &decoded);
        if (samples > 0 && decoded) {
            out = audio.resampleTo44100Mono(decoded, samples, channels, sampleRate);
            std::free(decoded);
            return true;
        }
        return false;
    }

    void loadClassicSounds(mz_zip_archive* zip, AudioEngine& audio) {
        auto tryLoad = [&](const char* jarRel, SoundBuffer& dst) {
            std::vector<uint8_t> raw;
            if (readRawBytesFromJarOrDisk(zip, jarRel, raw)) {
                decodeOggToSoundBuffer(raw.data(), raw.size(), audio, dst);
            }
        };
        tryLoad("sound/step/stone1.ogg",   digSounds[SND_STONE]);
        tryLoad("sound/step/grass1.ogg",   digSounds[SND_GRASS]);
        tryLoad("sound/step/gravel1.ogg",  digSounds[SND_GRAVEL]);
        tryLoad("sound/step/wood1.ogg",    digSounds[SND_WOOD]);
        tryLoad("sound/step/sand1.ogg",    digSounds[SND_SAND]);
        tryLoad("sound/step/snow1.ogg",    digSounds[SND_SNOW]);
        tryLoad("sound/step/cloth1.ogg",   digSounds[SND_CLOTH]);
        digSounds[SND_METAL] = digSounds[SND_STONE];
        digSounds[SND_GLASS] = digSounds[SND_STONE];
        for (int i = 0; i < 9; ++i) stepSounds[i] = digSounds[i];

        tryLoad("sound/random/hurt.ogg",       hitSound);
        tryLoad("sound/random/explode.ogg",    explodeSound);
        tryLoad("sound/random/bow.ogg",        bowSound);
        tryLoad("sound/random/pop.ogg",        popSound);
        tryLoad("sound/random/click.ogg",      clickSound);
        tryLoad("sound/random/door_open.ogg",  doorOpenSound);
        tryLoad("sound/random/door_close.ogg", doorCloseSound);
        tryLoad("sound/random/fizz.ogg",       fizzSound);
    }

    void regItem(uint16_t id, const char* name, uint16_t sprIdx, uint8_t maxStack = 64,
                 uint16_t maxDmg = 0, ToolCategory tool = TOOL_NONE, uint8_t harvest = 0,
                 float speed = 1.0f, float atk = 1.0f, int heal = 0,
                 ArmorSlotType armor = ARMOR_NONE, int armorPts = 0, uint8_t placesBlk = 0) {
        ItemDef& it = (id == ITEM_RECORD_13) ? record13Def : ((id == ITEM_RECORD_CAT) ? recordCatDef : itemDefs[id]);
        it.name         = name;
        it.spriteIdx    = sprIdx;
        it.maxStackSize = maxStack;
        it.maxDamage    = maxDmg;
        it.toolType     = tool;
        it.harvestLevel = harvest;
        it.miningSpeed  = speed;
        it.attackDamage = atk;
        it.healAmount   = heal;
        it.armorSlot    = armor;
        it.armorPoints  = armorPts;
        it.placesBlock  = placesBlk;
    }

    void registerAllBeta173Items() {
        regItem(ITEM_WOOD_SHOVEL,    "Wooden Shovel",   64, 1, 59,   TOOL_SHOVEL,  1, 2.0f, 2.0f);
        regItem(ITEM_WOOD_PICKAXE,   "Wooden Pickaxe",  80, 1, 59,   TOOL_PICKAXE, 1, 2.0f, 2.0f);
        regItem(ITEM_WOOD_AXE,       "Wooden Axe",      96, 1, 59,   TOOL_AXE,     1, 2.0f, 3.0f);
        regItem(ITEM_WOOD_SWORD,     "Wooden Sword",    48, 1, 59,   TOOL_SWORD,   0, 1.5f, 4.0f);
        regItem(ITEM_WOOD_HOE,       "Wooden Hoe",     112, 1, 59);

        regItem(ITEM_STONE_SHOVEL,   "Stone Shovel",    65, 1, 131,  TOOL_SHOVEL,  2, 4.0f, 3.0f);
        regItem(ITEM_STONE_PICKAXE,  "Stone Pickaxe",   81, 1, 131,  TOOL_PICKAXE, 2, 4.0f, 3.0f);
        regItem(ITEM_STONE_AXE,      "Stone Axe",       97, 1, 131,  TOOL_AXE,     2, 4.0f, 4.0f);
        regItem(ITEM_STONE_SWORD,    "Stone Sword",     49, 1, 131,  TOOL_SWORD,   0, 1.5f, 5.0f);
        regItem(ITEM_STONE_HOE,      "Stone Hoe",      113, 1, 131);

        regItem(ITEM_IRON_SHOVEL,    "Iron Shovel",     66, 1, 250,  TOOL_SHOVEL,  3, 6.0f, 4.0f);
        regItem(ITEM_IRON_PICKAXE,   "Iron Pickaxe",    82, 1, 250,  TOOL_PICKAXE, 3, 6.0f, 4.0f);
        regItem(ITEM_IRON_AXE,       "Iron Axe",        98, 1, 250,  TOOL_AXE,     3, 6.0f, 5.0f);
        regItem(ITEM_IRON_SWORD,     "Iron Sword",      50, 1, 250,  TOOL_SWORD,   0, 1.5f, 6.0f);
        regItem(ITEM_IRON_HOE,       "Iron Hoe",       114, 1, 250);

        regItem(ITEM_DIAMOND_SHOVEL, "Diamond Shovel",  67, 1, 1561, TOOL_SHOVEL,  4, 8.0f, 5.0f);
        regItem(ITEM_DIAMOND_PICKAXE,"Diamond Pickaxe", 83, 1, 1561, TOOL_PICKAXE, 4, 8.0f, 5.0f);
        regItem(ITEM_DIAMOND_AXE,    "Diamond Axe",     99, 1, 1561, TOOL_AXE,     4, 8.0f, 6.0f);
        regItem(ITEM_DIAMOND_SWORD,  "Diamond Sword",   51, 1, 1561, TOOL_SWORD,   0, 1.5f, 7.0f);
        regItem(ITEM_DIAMOND_HOE,    "Diamond Hoe",    115, 1, 1561);

        regItem(ITEM_GOLD_SHOVEL,    "Golden Shovel",   68, 1, 32,   TOOL_SHOVEL,  1, 12.0f, 2.0f);
        regItem(ITEM_GOLD_PICKAXE,   "Golden Pickaxe",  84, 1, 32,   TOOL_PICKAXE, 1, 12.0f, 2.0f);
        regItem(ITEM_GOLD_AXE,       "Golden Axe",     100, 1, 32,   TOOL_AXE,     1, 12.0f, 3.0f);
        regItem(ITEM_GOLD_SWORD,     "Golden Sword",    52, 1, 32,   TOOL_SWORD,   0, 1.5f, 4.0f);
        regItem(ITEM_GOLD_HOE,       "Golden Hoe",     116, 1, 32);

        regItem(ITEM_FLINT_AND_STEEL, "Flint and Steel",  5, 1, 64);
        regItem(ITEM_BOW,             "Bow",             21, 1, 384);
        regItem(ITEM_ARROW,           "Arrow",           37, 64);
        regItem(ITEM_FISHING_ROD,     "Fishing Rod",     69, 1, 64);
        regItem(ITEM_SHEARS,          "Shears",         133, 1, 238, TOOL_SHEARS, 1, 5.0f);
        regItem(ITEM_BUCKET,          "Bucket",          74, 16);
        regItem(ITEM_WATER_BUCKET,    "Water Bucket",    75, 1);
        regItem(ITEM_LAVA_BUCKET,     "Lava Bucket",     76, 1);
        regItem(ITEM_MILK_BUCKET,     "Milk",            77, 1);
        regItem(ITEM_COMPASS,         "Compass",         54, 64);
        regItem(ITEM_CLOCK,           "Clock",           70, 64);
        regItem(ITEM_MAP,             "Map",             12, 1);
        regItem(ITEM_MINECART,        "Minecart",       135, 1);
        regItem(ITEM_CHEST_MINECART,  "Minecart with Chest", 151, 1);
        regItem(ITEM_FURNACE_MINECART,"Minecart with Furnace", 167, 1);
        regItem(ITEM_BOAT,            "Boat",           136, 1);
        regItem(ITEM_SADDLE,          "Saddle",         138, 1);

        regItem(ITEM_APPLE,           "Apple",           10, 1, 0, TOOL_NONE, 0, 1.0f, 1.0f, 4);
        regItem(ITEM_MUSHROOM_STEW,   "Mushroom Stew",   72, 1, 0, TOOL_NONE, 0, 1.0f, 1.0f, 10);
        regItem(ITEM_BREAD,           "Bread",           41, 1, 0, TOOL_NONE, 0, 1.0f, 1.0f, 5);
        regItem(ITEM_PORKCHOP_RAW,    "Raw Porkchop",    87, 1, 0, TOOL_NONE, 0, 1.0f, 1.0f, 3);
        regItem(ITEM_PORKCHOP_COOKED, "Cooked Porkchop", 88, 1, 0, TOOL_NONE, 0, 1.0f, 1.0f, 8);
        regItem(ITEM_GOLDEN_APPLE,    "Golden Apple",    11, 1, 0, TOOL_NONE, 0, 1.0f, 1.0f, 20);
        regItem(ITEM_FISH_RAW,        "Raw Fish",        89, 1, 0, TOOL_NONE, 0, 1.0f, 1.0f, 2);
        regItem(ITEM_FISH_COOKED,     "Cooked Fish",     90, 1, 0, TOOL_NONE, 0, 1.0f, 1.0f, 5);
        regItem(ITEM_COOKIE,          "Cookie",          92, 8, 0, TOOL_NONE, 0, 1.0f, 1.0f, 1);

        regItem(ITEM_COAL,            "Coal",             7, 64);
        regItem(ITEM_DIAMOND,         "Diamond",         55, 64);
        regItem(ITEM_IRON_INGOT,      "Iron Ingot",      23, 64);
        regItem(ITEM_GOLD_INGOT,      "Gold Ingot",      39, 64);
        regItem(ITEM_STICK,           "Stick",           53, 64);
        regItem(ITEM_BOWL,            "Bowl",            71, 64);
        regItem(ITEM_STRING,          "String",           8, 64);
        regItem(ITEM_FEATHER,         "Feather",         24, 64);
        regItem(ITEM_GUNPOWDER,       "Sulphur",         40, 64);
        regItem(ITEM_SEEDS,           "Seeds",            9, 64);
        regItem(ITEM_WHEAT,           "Wheat",           25, 64);
        regItem(ITEM_FLINT,           "Flint",            6, 64);
        regItem(ITEM_PAINTING,        "Painting",        26, 64);
        regItem(ITEM_SIGN,            "Sign",            42, 1,  0, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_NONE, 0, 63);
        regItem(ITEM_WOOD_DOOR,       "Wooden Door",     43, 1,  0, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_NONE, 0, 64);
        regItem(ITEM_IRON_DOOR,       "Iron Door",       44, 1,  0, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_NONE, 0, 71);
        regItem(ITEM_REDSTONE,        "Redstone",        56, 64, 0, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_NONE, 0, 55);
        regItem(ITEM_SNOWBALL,        "Snowball",        14, 16);
        regItem(ITEM_LEATHER,         "Leather",        103, 64);
        regItem(ITEM_BRICK_INGOT,     "Brick",           22, 64);
        regItem(ITEM_CLAY_BALL,       "Clay",            57, 64);
        regItem(ITEM_REEDS,           "Sugar Canes",     27, 64, 0, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_NONE, 0, 83);
        regItem(ITEM_PAPER,           "Paper",           58, 64);
        regItem(ITEM_BOOK,            "Book",            59, 64);
        regItem(ITEM_SLIME_BALL,      "Slimeball",       30, 64);
        regItem(ITEM_EGG,             "Egg",             12, 16);
        regItem(ITEM_GLOWSTONE_DUST,  "Glowstone Dust",  73, 64);
        regItem(ITEM_DYE,             "Dye",            110, 64);
        regItem(ITEM_BONE,            "Bone",           124, 64);
        regItem(ITEM_SUGAR,           "Sugar",          125, 64);
        regItem(ITEM_CAKE_ITEM,       "Cake",           126, 1,  0, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_NONE, 0, 92);
        regItem(ITEM_BED_ITEM,        "Bed",            142, 1,  0, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_NONE, 0, 26);
        regItem(ITEM_REPEATER_ITEM,   "Redstone Repeater",134,64,0, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_NONE, 0, 93);

        regItem(ITEM_LEATHER_HELMET,  "Leather Cap",      0, 1, 34,  TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_HELMET, 3);
        regItem(ITEM_LEATHER_CHEST,   "Leather Tunic",   16, 1, 49,  TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_CHEST,  8);
        regItem(ITEM_LEATHER_LEGS,    "Leather Pants",   32, 1, 46,  TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_LEGS,   6);
        regItem(ITEM_LEATHER_BOOTS,   "Leather Boots",   48, 1, 40,  TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_BOOTS,  3);

        regItem(ITEM_CHAIN_HELMET,    "Chain Helmet",     1, 1, 67,  TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_HELMET, 3);
        regItem(ITEM_CHAIN_CHEST,     "Chain Chestplate",17, 1, 97,  TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_CHEST,  8);
        regItem(ITEM_CHAIN_LEGS,      "Chain Leggings",  33, 1, 91,  TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_LEGS,   6);
        regItem(ITEM_CHAIN_BOOTS,     "Chain Boots",     49, 1, 79,  TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_BOOTS,  3);

        regItem(ITEM_IRON_HELMET,     "Iron Helmet",      2, 1, 134, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_HELMET, 3);
        regItem(ITEM_IRON_CHEST,      "Iron Chestplate", 18, 1, 193, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_CHEST,  8);
        regItem(ITEM_IRON_LEGS,       "Iron Leggings",   34, 1, 181, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_LEGS,   6);
        regItem(ITEM_IRON_BOOTS,      "Iron Boots",      50, 1, 157, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_BOOTS,  3);

        regItem(ITEM_DIAMOND_HELMET,  "Diamond Helmet",   3, 1, 268, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_HELMET, 3);
        regItem(ITEM_DIAMOND_CHEST,   "Diamond Chestplate",19,1,385, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_CHEST,  8);
        regItem(ITEM_DIAMOND_LEGS,    "Diamond Leggings",35, 1, 361, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_LEGS,   6);
        regItem(ITEM_DIAMOND_BOOTS,   "Diamond Boots",   51, 1, 313, TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_BOOTS,  3);

        regItem(ITEM_GOLD_HELMET,     "Golden Helmet",    4, 1, 67,  TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_HELMET, 3);
        regItem(ITEM_GOLD_CHEST,      "Golden Chestplate",20,1, 97,  TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_CHEST,  8);
        regItem(ITEM_GOLD_LEGS,       "Golden Leggings", 36, 1, 91,  TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_LEGS,   6);
        regItem(ITEM_GOLD_BOOTS,      "Golden Boots",    52, 1, 79,  TOOL_NONE, 0, 1.0f, 1.0f, 0, ARMOR_BOOTS,  3);

        regItem(ITEM_RECORD_13,       "Music Disc (13)",  240, 1);
        regItem(ITEM_RECORD_CAT,      "Music Disc (cat)", 241, 1);
    }
};