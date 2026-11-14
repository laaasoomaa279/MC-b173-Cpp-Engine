#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "assets/recipes_b173.hpp"
#include "world/mcregion_nbt.hpp"
#include "world/world.hpp"
#include "world/world_gen_b173.hpp"
#include "render/mesher_cubes.hpp"
#include "render/mesher_models.hpp"
#include "render/sky_weather.hpp"
#include "gameplay/physics.hpp"
#include "gameplay/redstone_pistons.hpp"
#include "gameplay/combat_nether.hpp"
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
// Native C ABI Modding Interface (Allows loading mods/*.dll at runtime)
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

static WorldMap*                        g_modWorldPtr = nullptr;
static BetaInventoryAndHud*             g_modHudPtr   = nullptr;
static MPSCQueue<ChunkCoord, 2048>      g_meshRequestQueue;
static MPSCQueue<SectionMeshData, 1024> g_readyMeshQueue;

static inline void enqueueBlockAndNeighborChunks(int wx, int wz) {
    int32_t cx = wx >> 4;
    int32_t cz = wz >> 4;
    g_meshRequestQueue.push({ cx, cz });
    if ((wx & 15) == 0)  g_meshRequestQueue.push({ cx - 1, cz });
    if ((wx & 15) == 15) g_meshRequestQueue.push({ cx + 1, cz });
    if ((wz & 15) == 0)  g_meshRequestQueue.push({ cx, cz - 1 });
    if ((wz & 15) == 15) g_meshRequestQueue.push({ cx, cz + 1 });
}

extern "C" {
    static void modApiRegisterBlock(uint8_t id, const char* name, uint16_t spr, float hardness, uint8_t lightEmit) {
        if (g_modWorldPtr && id >= 97) {
            g_modWorldPtr->registry.registerBlock(id, name, SHAPE_CUBE, spr, spr, spr, hardness, hardness * 3.0f, lightEmit, 15, true, true);
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
                auto initFn = (McBetaModInitFn)GetProcAddress(modDll, "ModInit");
                if (initFn) initFn(&api);
            }
        }
    }
}

// ============================================================================
// Mojang ItemRenderer.java & RenderGlobal.java Exact First-Person & Overlays
// ============================================================================
class FirstPersonAndOverlayRenderer {
public:
    GLuint wireShader = 0, wireVao = 0, wireVbo = 0;
    GLuint texShader  = 0, texVao  = 0, texVbo  = 0;

    // Notch ItemRenderer.java equippedProgress & swing interpolation state
    float equippedProgress     = 1.0f;
    float prevEquippedProgress = 1.0f;
    float prevSwingProgress    = 0.0f;
    int   lastEquippedSlot     = -1;
    uint16_t lastEquippedId    = 0;

    struct TexVert {
        float x, y, z;
        float u, v;
        float r, g, b, a;
    };

    void init() {
        const char* wVert = R"(
            #version 330 core
            layout(location = 0) in vec3 a_pos;
            uniform mat4 u_mvp;
            void main() { gl_Position = u_mvp * vec4(a_pos, 1.0); }
        )";
        const char* wFrag = R"(
            #version 330 core
            out vec4 FragColor;
            void main() { FragColor = vec4(0.0, 0.0, 0.0, 0.45); }
        )";
        GLuint vs = compileShaderStage(GL_VERTEX_SHADER, wVert, "wire.vert");
        GLuint fs = compileShaderStage(GL_FRAGMENT_SHADER, wFrag, "wire.frag");
        if (vs && fs) {
            wireShader = glCreateProgram();
            glAttachShader(wireShader, vs);
            glAttachShader(wireShader, fs);
            glLinkProgram(wireShader);
            glDeleteShader(vs);
            glDeleteShader(fs);
        }
        glGenVertexArrays(1, &wireVao);
        glGenBuffers(1, &wireVbo);

        const char* tVert = R"(
            #version 330 core
            layout(location = 0) in vec3 a_pos;
            layout(location = 1) in vec2 a_uv;
            layout(location = 2) in vec4 a_col;
            uniform mat4 u_mvp;
            out vec2 v_uv;
            out vec4 v_col;
            void main() {
                v_uv = a_uv;
                v_col = a_col;
                gl_Position = u_mvp * vec4(a_pos, 1.0);
            }
        )";
        const char* tFrag = R"(
            #version 330 core
            in vec2 v_uv;
            in vec4 v_col;
            uniform sampler2D u_tex;
            out vec4 FragColor;
            void main() {
                vec4 c = texture(u_tex, v_uv) * v_col;
                if (c.a < 0.05) discard;
                FragColor = c;
            }
        )";
        GLuint tvs = compileShaderStage(GL_VERTEX_SHADER, tVert, "fp.vert");
        GLuint tfs = compileShaderStage(GL_FRAGMENT_SHADER, tFrag, "fp.frag");
        if (tvs && tfs) {
            texShader = glCreateProgram();
            glAttachShader(texShader, tvs);
            glAttachShader(texShader, tfs);
            glLinkProgram(texShader);
            glDeleteShader(tvs);
            glDeleteShader(tfs);
        }
        glGenVertexArrays(1, &texVao);
        glGenBuffers(1, &texVbo);
    }

    // Exact Mojang ItemRenderer.updateEquippedItem() (20 TPS tick)
    void tickEquippedItem(const PlayerPhysics& player, int currentSlot, const ItemStack& held) {
        prevEquippedProgress = equippedProgress;
        prevSwingProgress    = player.swingProgress;
        if (prevSwingProgress > player.swingProgress) {
            prevSwingProgress = 0.0f; // Wrap cleanly on new swing start
        }

        bool sameItem = (currentSlot == lastEquippedSlot) && (held.id == lastEquippedId);
        float target  = sameItem ? 1.0f : 0.0f;
        float delta   = std::clamp(target - equippedProgress, -0.4f, 0.4f);
        equippedProgress += delta;

        if (equippedProgress < 0.1f) {
            lastEquippedSlot = currentSlot;
            lastEquippedId   = held.id;
        }
    }

    void drawSelectionBox(const Mat4& viewProj, const Vec3& eyePos, int bx, int by, int bz) {
        if (!wireShader) return;
        float e = 0.0025f;
        float x0 = (bx - e) - eyePos.x, x1 = (bx + 1.0f + e) - eyePos.x;
        float y0 = (by - e) - eyePos.y, y1 = (by + 1.0f + e) - eyePos.y;
        float z0 = (bz - e) - eyePos.z, z1 = (bz + 1.0f + e) - eyePos.z;

        float lines[24 * 3] = {
            x0,y0,z0, x1,y0,z0,  x1,y0,z0, x1,y0,z1,  x1,y0,z1, x0,y0,z1,  x0,y0,z1, x0,y0,z0,
            x0,y1,z0, x1,y1,z0,  x1,y1,z0, x1,y1,z1,  x1,y1,z1, x0,y1,z1,  x0,y1,z1, x0,y1,z0,
            x0,y0,z0, x0,y1,z0,  x1,y0,z0, x1,y1,z0,  x1,y0,z1, x1,y1,z1,  x0,y0,z1, x0,y1,z1
        };

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(2.0f);
        glUseProgram(wireShader);
        glUniformMatrix4fv(glGetUniformLocation(wireShader, "u_mvp"), 1, GL_FALSE, viewProj.m);
        glBindVertexArray(wireVao);
        glBindBuffer(GL_ARRAY_BUFFER, wireVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(lines), lines, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glDrawArrays(GL_LINES, 0, 24);
        glDisable(GL_BLEND);
    }

    // Mojang RenderGlobal.drawBlockDamageTexture (SPR_DESTROY_0..9 = 240..249)
    void drawBlockBreakCracks(const Mat4& viewProj, const BetaAssetManager& assets,
                              const Vec3& eyePos, int bx, int by, int bz, float breakProgress) {
        if (!texShader || !assets.terrainTex || breakProgress <= 0.01f) return;
        int stage = std::clamp((int)(breakProgress * 10.0f), 0, 9);
        uint16_t spr = (uint16_t)(SPR_DESTROY_0 + stage);

        float u0 = (spr % 16) / 16.0f + 0.001f;
        float v0 = (spr / 16) / 16.0f + 0.001f;
        float u1 = (spr % 16 + 1) / 16.0f - 0.001f;
        float v1 = (spr / 16 + 1) / 16.0f - 0.001f;

        float e = 0.003f;
        float x0 = (bx - e) - eyePos.x, x1 = (bx + 1.0f + e) - eyePos.x;
        float y0 = (by - e) - eyePos.y, y1 = (by + 1.0f + e) - eyePos.y;
        float z0 = (bz - e) - eyePos.z, z1 = (bz + 1.0f + e) - eyePos.z;

        std::vector<TexVert> v;
        v.reserve(36);
        auto pushFace = [&](float p[4][3]) {
            TexVert q[4] = {
                {p[0][0],p[0][1],p[0][2], u0,v1, 1,1,1,0.75f},
                {p[1][0],p[1][1],p[1][2], u1,v1, 1,1,1,0.75f},
                {p[2][0],p[2][1],p[2][2], u1,v0, 1,1,1,0.75f},
                {p[3][0],p[3][1],p[3][2], u0,v0, 1,1,1,0.75f}
            };
            v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]);
            v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
        };

        float fE[4][3]={{x1,y0,z1},{x1,y0,z0},{x1,y1,z0},{x1,y1,z1}}; pushFace(fE);
        float fW[4][3]={{x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0}}; pushFace(fW);
        float fT[4][3]={{x0,y1,z1},{x1,y1,z1},{x1,y1,z0},{x0,y1,z0}}; pushFace(fT);
        float fB[4][3]={{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}}; pushFace(fB);
        float fS[4][3]={{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}}; pushFace(fS);
        float fN[4][3]={{x1,y0,z0},{x0,y0,z0},{x0,y1,z0},{x1,y1,z0}}; pushFace(fN);

        glEnable(GL_BLEND);
        glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
        glDepthMask(GL_FALSE);
        glUseProgram(texShader);
        glUniformMatrix4fv(glGetUniformLocation(texShader, "u_mvp"), 1, GL_FALSE, viewProj.m);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, assets.terrainTex);
        submitTexVerts(v);
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_BLEND);
    }

    // Exact Notch ItemRenderer.renderItemInFirstPerson(float partialTicks)
    void drawFirstPersonHeld(const WorldMap& world, const BetaAssetManager& assets,
                             const PlayerPhysics& player, const ItemStack& held,
                             float aspect, float alpha) {
        if (!texShader) return;

        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        const float PI = 3.14159265f;
        float equip = prevEquippedProgress + (equippedProgress - prevEquippedProgress) * alpha;
        float swing = prevSwingProgress + (player.swingProgress - prevSwingProgress) * alpha;
        float wd    = player.prevWalkDist + (player.walkDist - player.prevWalkDist) * alpha;
        float bob   = player.prevCameraYaw + (player.cameraYaw - player.prevCameraYaw) * alpha;

        // Notch exact EntityRenderer.setupViewBobbing + ItemRenderer swing trigonometry
        float bobX = -std::cos(wd * PI) * bob * 0.5f;
        float bobY =  std::abs(std::sin(wd * PI)) * bob * 0.5f;
        float bobRoll  = std::sin(wd * PI) * bob * 3.0f;
        float bobPitch = std::abs(std::cos(wd * PI - 0.2f) * bob) * 5.0f;

        float f1 = std::sin(swing * PI);
        float f2 = std::sin(std::sqrt(swing) * PI);
        float f3 = std::sin(swing * swing * PI);

        int bx = (int)std::floor(player.x), by = (int)std::floor(player.y + 1.0f), bz = (int)std::floor(player.z);
        float sky = (world.getSkyLightAt(bx, by, bz) / 15.0f) * world.getSkyBrightness();
        float blk = world.getBlockLightAt(bx, by, bz) / 15.0f;
        float light = std::clamp(std::max(sky, blk), 0.15f, 1.0f);

        Mat4 proj = Mat4::perspective(70.0f, aspect, 0.05f, 10.0f) *
                    Mat4::rotateZ(bobRoll) *
                    Mat4::rotateX(bobPitch);

        glUseProgram(texShader);
        glActiveTexture(GL_TEXTURE0);

        if (!held.isEmpty()) {
            // Notch ItemRenderer.renderItemInFirstPerson for Held Item / Block:
            // glTranslatef(0.56F, -0.52F - (1.0F - equip) * 0.6F, -0.72F);
            // glRotatef(45.0F, 0.0F, 1.0F, 0.0F);
            // glRotatef(-f3 * 20.0F, 0.0F, 1.0F, 0.0F);
            // glRotatef(-f2 * 20.0F, 0.0F, 0.0F, 1.0F);
            // glRotatef(-f2 * 80.0F, 1.0F, 0.0F, 0.0F);
            Mat4 baseItem = Mat4::translate(0.56f + bobX - f2 * 0.4f,
                                            -0.52f - bobY - (1.0f - equip) * 0.6f + f1 * 0.2f,
                                            -0.72f - f1 * 0.2f) *
                            Mat4::rotateY(45.0f) *
                            Mat4::rotateY(-f3 * 20.0f) *
                            Mat4::rotateZ(-f2 * 20.0f) *
                            Mat4::rotateX(-f2 * 80.0f);

            if (held.id < 256) {
                uint16_t rawSt = (uint16_t)((held.id << 4) | (held.damage & 0x0F));
                const BlockDef& def = world.registry.get(rawSt);
                if (def.shape == SHAPE_CUBE || def.shape == SHAPE_SLAB || def.shape == SHAPE_STAIR || def.shape == SHAPE_PISTON) {
                    Mat4 mvp = proj * baseItem * Mat4::scale(0.40f, 0.40f, 0.40f);
                    glUniformMatrix4fv(glGetUniformLocation(texShader, "u_mvp"), 1, GL_FALSE, mvp.m);
                    glBindTexture(GL_TEXTURE_2D, assets.terrainTex);

                    float tr = light, tg = light, tb = light;
                    if (def.tintMode == TINT_GRASS || def.tintMode == TINT_FOLIAGE) {
                        float br, bg, bb;
                        assets.sampleBiomeColor(0.7f, 0.7f, def.tintMode == TINT_FOLIAGE, br, bg, bb);
                        tr *= br; tg *= bg; tb *= bb;
                    }

                    std::vector<TexVert> v;
                    auto addBlockFace = [&](uint16_t spr, float p[4][3], float r, float g, float b) {
                        float u0 = (spr % 16) / 16.0f + 0.001f, v0 = (spr / 16) / 16.0f + 0.001f;
                        float u1 = (spr % 16 + 1) / 16.0f - 0.001f, v1 = (spr / 16 + 1) / 16.0f - 0.001f;
                        TexVert q[4] = {
                            {p[0][0],p[0][1],p[0][2], u0,v1, r,g,b,1},
                            {p[1][0],p[1][1],p[1][2], u1,v1, r,g,b,1},
                            {p[2][0],p[2][1],p[2][2], u1,v0, r,g,b,1},
                            {p[3][0],p[3][1],p[3][2], u0,v0, r,g,b,1}
                        };
                        v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]);
                        v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
                    };

                    float h = 0.5f;
                    float fT[4][3]={{-h,h,h},{h,h,h},{h,h,-h},{-h,h,-h}};
                    float fS[4][3]={{-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}};
                    float fW[4][3]={{-h,-h,-h},{-h,-h,h},{-h,h,h},{-h,h,-h}};
                    addBlockFace(world.registry.resolveFaceTexture(rawSt, FACE_TOP),   fT, tr, tg, tb);
                    addBlockFace(world.registry.resolveFaceTexture(rawSt, FACE_SOUTH), fS, light * 0.8f, light * 0.8f, light * 0.8f);
                    addBlockFace(world.registry.resolveFaceTexture(rawSt, FACE_WEST),  fW, light * 0.6f, light * 0.6f, light * 0.6f);
                    submitTexVerts(v);
                    glEnable(GL_CULL_FACE);
                    return;
                }
            }

            // Mojang ItemRenderer exact 3D Extruded Handheld Tool/Item orientation:
            // glScalef(0.4F, 0.4F, 0.4F); glRotatef(50.0F, 0,1,0); glRotatef(335.0F, 0,0,1); glTranslatef(-0.9375F, -0.0625F, 0.0F);
            GLuint tex = (held.id < 256) ? assets.terrainTex : assets.itemsTex;
            uint16_t spr = (held.id < 256)
                ? world.registry.resolveFaceTexture((uint16_t)((held.id << 4) | (held.damage & 0x0F)), FACE_NORTH)
                : assets.getItemDef(held.id).spriteIdx;

            Mat4 mvp = proj * baseItem *
                       Mat4::scale(0.42f, 0.42f, 0.42f) *
                       Mat4::rotateY(50.0f) *
                       Mat4::rotateZ(335.0f) *
                       Mat4::translate(-0.15f, 0.15f, 0.0f);

            glUniformMatrix4fv(glGetUniformLocation(texShader, "u_mvp"), 1, GL_FALSE, mvp.m);
            glBindTexture(GL_TEXTURE_2D, tex);

            float u0 = (spr % 16) / 16.0f + 0.001f, v0 = (spr / 16) / 16.0f + 0.001f;
            float u1 = (spr % 16 + 1) / 16.0f - 0.001f, v1 = (spr / 16 + 1) / 16.0f - 0.001f;
            float d = 0.035f;
            std::vector<TexVert> v = {
                {-0.5f,-0.5f, d, u0,v1, light,light,light,1}, {0.5f,-0.5f, d, u1,v1, light,light,light,1}, {0.5f,0.5f, d, u1,v0, light,light,light,1},
                {-0.5f,-0.5f, d, u0,v1, light,light,light,1}, {0.5f, 0.5f, d, u1,v0, light,light,light,1}, {-0.5f,0.5f, d, u0,v0, light,light,light,1},
                {-0.5f,-0.5f,-d, u0,v1, light*0.8f,light*0.8f,light*0.8f,1}, {0.5f,-0.5f,-d, u1,v1, light*0.8f,light*0.8f,light*0.8f,1}, {0.5f,0.5f,-d, u1,v0, light*0.8f,light*0.8f,light*0.8f,1},
                {-0.5f,-0.5f,-d, u0,v1, light*0.8f,light*0.8f,light*0.8f,1}, {0.5f, 0.5f,-d, u1,v0, light*0.8f,light*0.8f,light*0.8f,1}, {-0.5f,0.5f,-d, u0,v0, light*0.8f,light*0.8f,light*0.8f,1}
            };
            submitTexVerts(v);
        } else if (assets.mobSkins[SKIN_PLAYER]) {
            // Exact Mojang ItemRenderer.renderItemInFirstPerson for Empty Hand (Steve Right Arm)
            Mat4 armMat = proj *
                          Mat4::translate(0.64f + bobX - f2 * 0.3f,
                                          -0.60f - bobY - (1.0f - equip) * 0.6f + std::sin(swing * PI * 2.0f) * 0.4f,
                                          -0.72f - f1 * 0.4f) *
                          Mat4::rotateY(45.0f) *
                          Mat4::rotateZ(-f2 * 20.0f) *
                          Mat4::rotateY(f3 * 20.0f) *
                          Mat4::rotateX(-f2 * 80.0f) *
                          Mat4::translate(-0.12f, 0.25f, 0.12f) *
                          Mat4::rotateX(25.0f) *
                          Mat4::rotateZ(-15.0f) *
                          Mat4::scale(0.25f, 0.75f, 0.25f);

            glUniformMatrix4fv(glGetUniformLocation(texShader, "u_mvp"), 1, GL_FALSE, armMat.m);
            glBindTexture(GL_TEXTURE_2D, assets.mobSkins[SKIN_PLAYER]);

            std::vector<TexVert> v;
            auto addArmFace = [&](float p[4][3], int pxU0, int pxV0, int pxU1, int pxV1, float shade) {
                float u0 = pxU0 / 64.0f, v0 = pxV0 / 32.0f;
                float u1 = pxU1 / 64.0f, v1 = pxV1 / 32.0f;
                float c = light * shade;
                TexVert q[4] = {
                    {p[0][0],p[0][1],p[0][2], u0,v1, c,c,c,1},
                    {p[1][0],p[1][1],p[1][2], u1,v1, c,c,c,1},
                    {p[2][0],p[2][1],p[2][2], u1,v0, c,c,c,1},
                    {p[3][0],p[3][1],p[3][2], u0,v0, c,c,c,1}
                };
                v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]);
                v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
            };
            float h = 0.5f;
            float fT[4][3]={{-h,h,h},{h,h,h},{h,h,-h},{-h,h,-h}};
            float fW[4][3]={{-h,-h,-h},{-h,-h,h},{-h,h,h},{-h,h,-h}};
            float fS[4][3]={{-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}};
            addArmFace(fT, 44, 16, 48, 20, 1.0f);
            addArmFace(fW, 40, 20, 44, 32, 0.85f);
            addArmFace(fS, 44, 20, 48, 32, 0.75f);
            submitTexVerts(v);
        }

        glEnable(GL_CULL_FACE);
    }

private:
    void submitTexVerts(const std::vector<TexVert>& verts) {
        if (verts.empty()) return;
        glBindVertexArray(texVao);
        glBindBuffer(GL_ARRAY_BUFFER, texVbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(TexVert), verts.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TexVert), (void*)offsetof(TexVert, x));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TexVert), (void*)offsetof(TexVert, u));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TexVert), (void*)offsetof(TexVert, r));
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    }
};

static void executeSlashCommand(const std::string& cmdLine, WorldMap& world, PlayerPhysics& player,
                                BetaInventoryAndHud& hud, BetaSkyWeatherSystem& sky,
                                BetaEntityManager& entities, const BetaAssetManager& assets) {
    std::istringstream ss(cmdLine.substr(1));
    std::string cmd;
    ss >> cmd;

    if (cmd == "fly") {
        player.flying = !player.flying;
        hud.addChatMessage(player.flying ? "§aFlight enabled" : "§eFlight disabled");
    } else if (cmd == "heal") {
        player.health = 20.0f;
        hud.addChatMessage("§aHealed to full 10 hearts!");
    } else if (cmd == "kill") {
        player.health = 0.0f;
    } else if (cmd == "seed") {
        hud.addChatMessage("§eSeed: " + std::to_string(world.worldSeed));
    } else if (cmd == "time") {
        std::string sub; ss >> sub;
        if (sub == "day" || sub == "set") {
            int val = 1000;
            if (sub == "set") ss >> val;
            world.dayTime = (uint64_t)(val % 24000);
        } else if (sub == "night") {
            world.dayTime = 14000;
        }
        hud.addChatMessage("§aTime set to " + std::to_string(world.dayTime));
    } else if (cmd == "weather") {
        std::string w; ss >> w;
        sky.weather = (w == "rain") ? WEATHER_RAIN : ((w == "thunder") ? WEATHER_THUNDER : WEATHER_CLEAR);
        hud.addChatMessage("§aWeather updated");
    } else if (cmd == "tp") {
        float tx = player.x, ty = player.y, tz = player.z;
        if (ss >> tx >> ty >> tz) {
            player.x = player.prevX = tx;
            player.y = player.prevY = ty;
            player.z = player.prevZ = tz;
            hud.addChatMessage("§aTeleported!");
        }
    } else if (cmd == "give") {
        int id = 1, count = 64, meta = 0;
        ss >> id;
        if (!(ss >> count)) count = 1;
        ss >> meta;
        hud.giveItem(assets, (uint16_t)id, (uint16_t)meta, (uint8_t)std::clamp(count, 1, 64));
        hud.addChatMessage("§aGiven item ID " + std::to_string(id));
    } else if (cmd == "summon") {
        std::string mobName; ss >> mobName;
        BetaMobType mt = MOB_PIG;
        if (mobName == "zombie")        mt = MOB_ZOMBIE;
        else if (mobName == "creeper")  mt = MOB_CREEPER;
        else if (mobName == "skeleton") mt = MOB_SKELETON;
        else if (mobName == "spider")   mt = MOB_SPIDER;
        else if (mobName == "sheep")    mt = MOB_SHEEP;
        else if (mobName == "cow")      mt = MOB_COW;
        entities.spawnMob(mt, player.x + 2.0f, player.y, player.z + 2.0f);
        hud.addChatMessage("§aSummoned " + mobName);
    } else {
        hud.addChatMessage("§cCommands: /fly /give /tp /time /weather /summon /heal /seed");
    }
}

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
    unsigned int hwThreads = (conc > 2u) ? (conc - 1u) : 2u;
    workerPool.start(hwThreads);

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
    float tickAccumulator = 0.0f;
    float totalTimeSec    = 0.0f;
    float smoothedFps     = 60.0f;

    auto startOrLoadWorld = [&](const std::string& saveDir, const std::string& levelName, int64_t seedVal) {
        workerPool.clearPending();
        world.saveDir   = saveDir;
        world.worldSeed = seedVal;
        world.dimension = 0;

        for (int i = 0; i < GRID_DIM * GRID_DIM; ++i) {
            world.grid[i].state.store(CHUNK_EMPTY, std::memory_order_release);
        }
        for (auto& gs : gpuGrid) gs.valid = false;

        BetaLevelData lvl;
        if (McRegionStorage::loadLevelDat(saveDir, lvl)) {
            world.worldSeed = lvl.seed;
            world.dayTime   = (uint64_t)lvl.time;
            world.dimension = lvl.dimension;
            player.x = player.prevX = (float)lvl.playerX;
            player.y = player.prevY = (float)lvl.playerY;
            player.z = player.prevZ = (float)lvl.playerZ;
            player.yaw    = lvl.playerYaw;
            player.pitch  = lvl.playerPitch;
            player.health = (float)std::max<int16_t>(1, lvl.health);

            for (int i = 0; i < 36; ++i) hud.mainSlots[i].clear();
            for (const auto& sl : lvl.inventory) {
                if (sl.slot >= 0 && sl.slot < 36) {
                    hud.mainSlots[(int)sl.slot] = { (uint16_t)sl.id, (uint16_t)sl.damage, (uint8_t)sl.count };
                }
            }
        } else {
            player.x = player.prevX = 0.5f;
            player.y = player.prevY = 74.0f;
            player.z = player.prevZ = 0.5f;
            player.health = 20.0f;
            lvl.levelName = levelName;
            lvl.seed      = seedVal;
            McRegionStorage::saveLevelDat(saveDir, lvl);
        }

        worldGen.setSeed(world.worldSeed);
        int scx = ((int)std::floor(player.x)) >> 4;
        int scz = ((int)std::floor(player.z)) >> 4;
        worldGen.beginAreaGeneration(scx, scz, 1);
    };

    auto saveCompleteWorldState = [&]() {
        world.saveAllDirtyChunks();
        BetaLevelData lvl;
        lvl.levelName   = std::filesystem::path(world.saveDir).filename().string();
        lvl.seed        = world.worldSeed;
        lvl.time        = (int64_t)world.dayTime;
        lvl.dimension   = world.dimension;
        lvl.playerX     = player.x;
        lvl.playerY     = player.y;
        lvl.playerZ     = player.z;
        lvl.playerYaw   = player.yaw;
        lvl.playerPitch = player.pitch;
        lvl.health      = (int16_t)player.health;
        for (int i = 0; i < 36; ++i) {
            if (!hud.mainSlots[i].isEmpty()) {
                lvl.inventory.push_back({ (int8_t)i, (int16_t)hud.mainSlots[i].id, (int8_t)hud.mainSlots[i].count, (int16_t)hud.mainSlots[i].damage });
            }
        }
        McRegionStorage::saveLevelDat(world.saveDir, lvl);
    };

    while (window.running) {
        float dt = window.pumpEvents();
        totalTimeSec += dt;
        smoothedFps = smoothedFps * 0.92f + (1.0f / std::max(0.0005f, dt)) * 0.08f;
        world.renderDistance = settings.renderDistance;

        // 1. World Loading Progress
        if (menus.state == MENU_LOADING) {
            window.setMouseLock(false);
            if (worldGen.stepAreaGeneration(world, workerPool, g_meshRequestQueue, menus.loadingProg)) {
                int topY = world.getTopSolidY((int)std::floor(player.x), (int)std::floor(player.z));
                if (player.y < topY + 1.0f) player.y = player.prevY = (float)topY + 1.5f;
                menus.state      = MENU_IN_GAME;
                hud.activeScreen = GUI_NONE;
                window.setMouseLock(true);
            }
        }

        // 2. In-Game Input & Hotkeys
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
            }

            if (hud.activeScreen == GUI_NONE) {
                float sens = settings.mouseSensitivity * 0.22f;
                if (window.keys['C']) sens *= 0.30f;
                player.yaw   += window.rawMouseDX * sens;
                player.pitch -= window.rawMouseDY * sens;
                player.pitch  = std::clamp(player.pitch, -89.9f, 89.9f);

                if (window.wheelDelta != 0) {
                    hud.selectedHotbar = (hud.selectedHotbar - window.wheelDelta % 9 + 9) % 9;
                }
                for (int k = 0; k < 9; ++k) {
                    if (window.keysPressed['1' + k]) hud.selectedHotbar = k;
                }
            }

            // 3. Fixed 20 TPS Simulation Loop
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
                    player.tick(world, fwd, str, jmp, &audio, &assets);
                    world.tickSimulation(g_meshRequestQueue);
                    redstone.tick(world, g_meshRequestQueue);
                    skyWeather.tickWeather();
                    hud.tickChat();

                    for (const auto& dr : world.pendingDetachedDrops) {
                        entities.spawnDroppedItem(dr.itemId, dr.meta, dr.count, dr.x, dr.y, dr.z);
                    }
                    world.pendingDetachedDrops.clear();

                    entities.tickAll(world, player, combat, redstone, settings.difficulty,
                        [&](uint16_t id, uint16_t dmg, uint8_t cnt) { return hud.giveItem(assets, id, dmg, cnt); },
                        g_meshRequestQueue, &audio, &assets);

                    int32_t targetDim = world.dimension;
                    double tx = player.x, ty = player.y, tz = player.z;
                    if (combat.tickNetherPortalTransport(world, player, targetDim, tx, ty, tz)) {
                        saveCompleteWorldState();
                        workerPool.clearPending();
                        world.dimension = targetDim;
                        player.x = player.prevX = (float)tx;
                        player.y = player.prevY = (float)ty;
                        player.z = player.prevZ = (float)tz;
                        for (int i = 0; i < GRID_DIM * GRID_DIM; ++i) {
                            world.grid[i].state.store(CHUNK_EMPTY, std::memory_order_release);
                        }
                        for (auto& gs : gpuGrid) gs.valid = false;
                        worldGen.beginAreaGeneration(((int)std::floor(player.x)) >> 4, ((int)std::floor(player.z)) >> 4, 1,
                                                     true, (int)std::floor(player.x), (int)std::floor(player.y), (int)std::floor(player.z));
                        menus.state = MENU_LOADING;
                        break;
                    }

                    worldGen.updateInfiniteStreaming(world, workerPool, player.x, player.z, g_meshRequestQueue);
                }
            }
        }

        // 4. Deduplicate & Dispatch Mesh Requests to WorkerPool
        {
            std::unordered_set<uint64_t> seenChunks;
            ChunkCoord req;
            while (g_meshRequestQueue.pop(req)) {
                uint64_t key = ((uint64_t)(uint32_t)req.cx << 32) | (uint32_t)req.cz;
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

        // 5. Upload Ready Meshes to GPU
        int gpuUploads = 0;
        SectionMeshData ready;
        while (gpuUploads < 8 && g_readyMeshQueue.pop(ready)) {
            int slotIdx = (((ready.chunkZ & GRID_MASK) * GRID_DIM + (ready.chunkX & GRID_MASK)) * SECTIONS_PER_COL) + ready.secY;
            GpuSectionBuffer& gs = gpuGrid[(size_t)slotIdx];
            gs.builtChunkX = ready.chunkX;
            gs.builtChunkZ = ready.chunkZ;
            gs.valid       = true;

            gs.opaqueCount = (GLsizei)ready.opaqueVertices.size();
            if (gs.opaqueCount > 0) {
                if (!gs.opaqueVao) { glGenVertexArrays(1, &gs.opaqueVao); glGenBuffers(1, &gs.opaqueVbo); }
                glBindVertexArray(gs.opaqueVao);
                glBindBuffer(GL_ARRAY_BUFFER, gs.opaqueVbo);
                glBufferData(GL_ARRAY_BUFFER, gs.opaqueCount * sizeof(uint64_t), ready.opaqueVertices.data(), GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribIPointer(0, 2, GL_UNSIGNED_INT, sizeof(uint64_t), (void*)0);
            }

            gs.transCount = (GLsizei)ready.translucentVertices.size();
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

        // 6. Render 3D World
        Vec3 skyCol = world.getSkyColor();
        if (player.eyesInWater) skyCol = { 0.05f, 0.12f, 0.35f };
        glClearColor(skyCol.x, skyCol.y, skyCol.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspect = (float)window.width / (float)std::max(1, window.height);
        float alpha  = tickAccumulator / 0.05f;

        if (menus.state == MENU_IN_GAME) {
            Vec3 eyePos;
            player.getInterpolatedEyePos(alpha, eyePos.x, eyePos.y, eyePos.z);

            float yRad = player.yaw * 0.0174532925f;
            float pRad = player.pitch * 0.0174532925f;
            Vec3 lookDir{ std::cos(pRad) * std::cos(yRad), std::sin(pRad), std::cos(pRad) * std::sin(yRad) };

            float fov = (window.keys['C'] && hud.activeScreen == GUI_NONE) ? 22.0f : 70.0f;
            float farDist = (float)settings.renderDistance * 16.0f;

            // Exact Notch EntityRenderer.setupViewBobbing & hurtCameraEffect angles
            const float PI = 3.14159265f;
            float wd  = player.prevWalkDist + (player.walkDist - player.prevWalkDist) * alpha;
            float bob = player.prevCameraYaw + (player.cameraYaw - player.prevCameraYaw) * alpha;
            float bobRoll  = std::sin(wd * PI) * bob * 3.0f;
            float bobPitch = std::abs(std::cos(wd * PI - 0.2f) * bob) * 5.0f;

            float hurtRoll = 0.0f;
            if (player.hurtTime > 0) {
                float ht = ((float)player.hurtTime - alpha) / 10.0f;
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
                ItemStack& held = hud.getHeldStack();

                if (window.leftClicked) {
                    entities.performPlayerMeleeAttack(player, assets, held.id, eyePos, lookDir, &audio);
                }
                if (window.leftDown && hit.hit) {
                    player.triggerSwing();
                    if (player.flying) {
                        if (player.blockHitDelay == 0) {
                            uint16_t oldSt = world.getBlockAt(hit.bx, hit.by, hit.bz);
                            entities.spawnBlockBreakParticles(world, hit.bx, hit.by, hit.bz, oldSt);
                            world.setBlockAt(hit.bx, hit.by, hit.bz, 0, true);
                            redstone.onBlockChanged(world, hit.bx, hit.by, hit.bz, g_meshRequestQueue);
                            enqueueBlockAndNeighborChunks(hit.bx, hit.bz);
                            player.blockHitDelay = 5;
                        }
                    } else {
                        if (hit.bx != player.miningX || hit.by != player.miningY || hit.bz != player.miningZ) {
                            player.miningX = hit.bx; player.miningY = hit.by; player.miningZ = hit.bz;
                            player.breakProgress = 0.0f;
                        }
                        player.breakProgress += player.computeBlockDamagePerTick(world, assets, hit.bx, hit.by, hit.bz, held.id) * (dt * 20.0f);
                        if (player.breakProgress >= 1.0f) {
                            uint16_t brokenSt = world.getBlockAt(hit.bx, hit.by, hit.bz);
                            uint16_t dropId = 0, dropMeta = 0; uint8_t dropCnt = 0;
                            PlayerPhysics::resolveBlockDrops(world, assets, brokenSt, held.id, (uint32_t)world.worldTick, dropId, dropMeta, dropCnt);
                            if (dropId != 0 && dropCnt > 0) {
                                entities.spawnDroppedItem(dropId, dropMeta, dropCnt, hit.bx + 0.5f, hit.by + 0.5f, hit.bz + 0.5f);
                            }
                            entities.spawnBlockBreakParticles(world, hit.bx, hit.by, hit.bz, brokenSt);
                            world.setBlockAt(hit.bx, hit.by, hit.bz, 0, true);
                            redstone.onBlockChanged(world, hit.bx, hit.by, hit.bz, g_meshRequestQueue);
                            enqueueBlockAndNeighborChunks(hit.bx, hit.bz);
                            player.breakProgress = 0.0f;
                        }
                    }
                } else {
                    player.breakProgress = 0.0f;
                }

                if (window.rightClicked) {
                    const ItemDef& itDef = assets.getItemDef(held.id);
                    if (held.id >= 256 && itDef.healAmount > 0 && player.health < 20.0f) {
                        player.heal((float)itDef.healAmount);
                        if (held.id == ITEM_MUSHROOM_STEW) held = { ITEM_BOWL, 0, 1 };
                        else if (--held.count == 0) held.clear();
                    } else if (held.id == ITEM_BOW) {
                        for (int s = 0; s < 36; ++s) {
                            if (hud.mainSlots[s].id == ITEM_ARROW && hud.mainSlots[s].count > 0) {
                                if (--hud.mainSlots[s].count == 0) hud.mainSlots[s].clear();
                                combat.spawnProjectile(PROJ_ARROW, true, -1, eyePos, player.yaw, player.pitch, 1.55f);
                                if (!assets.bowSound.samples.empty()) audio.play2D(assets.bowSound, 1.0f, 1.0f);
                                break;
                            }
                        }
                    } else if (hit.hit) {
                        uint16_t targetSt = world.getBlockAt(hit.bx, hit.by, hit.bz);
                        uint8_t targetId  = (uint8_t)((targetSt >> 4) & 0xFF);

                        if (!player.sneaking && targetId == 58) {
                            hud.activeScreen = GUI_CRAFTING;
                            hud.updateCraftingPreview(recipes);
                            window.setMouseLock(false);
                        } else if (!player.sneaking && (targetId == 61 || targetId == 62)) {
                            hud.openTilePos  = { hit.bx, hit.by, hit.bz };
                            hud.activeScreen = GUI_FURNACE;
                            window.setMouseLock(false);
                        } else if (!player.sneaking && targetId == 54) {
                            hud.openTilePos  = { hit.bx, hit.by, hit.bz };
                            hud.activeScreen = GUI_CHEST;
                            window.setMouseLock(false);
                        } else if (!player.sneaking && redstone.interactWithBlock(world, hit.bx, hit.by, hit.bz, g_meshRequestQueue)) {
                            player.triggerSwing();
                        } else if (held.id == ITEM_FLINT_AND_STEEL) {
                            int px = hit.bx + hit.nx, py = hit.by + hit.ny, pz = hit.bz + hit.nz;
                            if (!combat.tryActivateNetherPortal(world, px, py, pz, g_meshRequestQueue)) {
                                world.setBlockAt(px, py, pz, (51 << 4), true);
                                enqueueBlockAndNeighborChunks(px, pz);
                            }
                        } else if (!held.isEmpty()) {
                            uint8_t placeId = (held.id < 256) ? (uint8_t)held.id : itDef.placesBlock;
                            if (placeId != 0) {
                                int px = hit.bx + hit.nx, py = hit.by + hit.ny, pz = hit.bz + hit.nz;
                                AABB blkBox{ (float)px, (float)py, (float)pz, px + 1.0f, py + 1.0f, pz + 1.0f };
                                AABB plrBox = AABB::fromFootPos(player.x, player.y, player.z, PlayerPhysics::HALF_WIDTH, PlayerPhysics::STAND_HEIGHT);
                                if (!world.registry.getById(placeId).isSolid || !plrBox.intersects(blkBox)) {
                                    uint8_t meta = (uint8_t)(held.damage & 0x0F);
                                    if (placeId == 29 || placeId == 33) {
                                        if (std::abs(player.pitch) > 55.0f) meta = (player.pitch > 0.0f) ? 0 : 1;
                                        else {
                                            int dir4 = ((int)std::floor(player.yaw / 90.0f + 0.5f)) & 3;
                                            const uint8_t pMap[4] = { 4, 2, 5, 3 };
                                            meta = pMap[dir4];
                                        }
                                    }
                                    world.setBlockAt(px, py, pz, (uint16_t)((placeId << 4) | meta), true);
                                    if (placeId == 64 || placeId == 71) {
                                        world.setBlockAt(px, py + 1, pz, (uint16_t)((placeId << 4) | 0x08), false);
                                    }
                                    redstone.onBlockChanged(world, px, py, pz, g_meshRequestQueue);
                                    enqueueBlockAndNeighborChunks(px, pz);
                                    player.triggerSwing();
                                    if (!player.flying && --held.count == 0) held.clear();
                                }
                            }
                        }
                    }
                }
            }

            // 6A. Sky Dome
            skyWeather.renderSky(world, assets, proj, view);

            // 6B. Chunk Meshes
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glUseProgram(chunkShader);
            glUniformMatrix4fv(glGetUniformLocation(chunkShader, "u_viewProj"), 1, GL_FALSE, viewProj.m);
            glUniform1f(glGetUniformLocation(chunkShader, "u_skyBrightness"), world.getSkyBrightness());
            glUniform1f(glGetUniformLocation(chunkShader, "u_torchFlicker"), std::sin(totalTimeSec * 12.0f) * 0.015f);
            glUniform3f(glGetUniformLocation(chunkShader, "u_fogColor"), skyCol.x, skyCol.y, skyCol.z);
            glUniform1f(glGetUniformLocation(chunkShader, "u_fogStart"), farDist * 0.55f);
            glUniform1f(glGetUniformLocation(chunkShader, "u_fogEnd"), farDist);
            glUniform1i(glGetUniformLocation(chunkShader, "u_fogMode"), player.eyesInWater ? 1 : 0);
            glUniform1f(glGetUniformLocation(chunkShader, "u_fogDensity"), 0.08f);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, assets.terrainTex);
            glUniform1i(glGetUniformLocation(chunkShader, "u_terrainTex"), 0);

            GLint uOffset  = glGetUniformLocation(chunkShader, "u_chunkOffset");
            GLint uGrass   = glGetUniformLocation(chunkShader, "u_grassColor");
            GLint uFoliage = glGetUniformLocation(chunkShader, "u_foliageColor");

            int pcx = ((int)std::floor(eyePos.x)) >> 4;
            int pcz = ((int)std::floor(eyePos.z)) >> 4;
            int rd  = settings.renderDistance;

            for (int pass = 0; pass < 2; ++pass) {
                if (pass == 1) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }
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
                            const GpuSectionBuffer& gs = gpuGrid[(size_t)slotIdx];
                            if (!gs.valid || gs.builtChunkX != cx || gs.builtChunkZ != cz) continue;

                            GLsizei cnt = (pass == 0) ? gs.opaqueCount : gs.transCount;
                            if (cnt == 0) continue;

                            float rx = (float)(cx * 16) - eyePos.x;
                            float ry = (float)(sy * 16) - eyePos.y;
                            float rz = (float)(cz * 16) - eyePos.z;
                            if (!frustum.isBoxVisible(rx, ry, rz, rx + 16.0f, ry + 16.0f, rz + 16.0f)) continue;

                            glUniform3f(uOffset, rx, ry, rz);
                            glBindVertexArray((pass == 0) ? gs.opaqueVao : gs.transVao);
                            glDrawArrays(GL_TRIANGLES, 0, cnt);
                        }
                    }
                }
                if (pass == 1) glDisable(GL_BLEND);
            }

            // 6C. Entities, Selection Box, Mining Cracks, Clouds, Weather & 3D Held Hand
            entityRenderer.renderAll(world, assets, entities, combat, redstone, viewProj, eyePos, alpha, totalTimeSec);
            if (hit.hit) {
                fpRenderer.drawSelectionBox(viewProj, eyePos, hit.bx, hit.by, hit.bz);
                if (player.breakProgress > 0.0f) {
                    fpRenderer.drawBlockBreakCracks(viewProj, assets, eyePos, hit.bx, hit.by, hit.bz, player.breakProgress);
                }
            }
            skyWeather.renderClouds(world, assets, viewProj, eyePos);
            skyWeather.renderWeather(world, assets, viewProj, eyePos);

            if (hud.activeScreen == GUI_NONE) {
                fpRenderer.drawFirstPersonHeld(world, assets, player, hud.getHeldStack(), aspect, alpha);
            }
        }

        // 7. Render 2D UI & Menus
        uiRenderer.beginFrame(window.width, window.height, settings.guiScale);
        int mx = window.mouseX / uiRenderer.guiScale;
        int my = window.mouseY / uiRenderer.guiScale;

        if (menus.state == MENU_IN_GAME) {
            hud.renderHUD(uiRenderer, world, assets, player, entities, smoothedFps);
            hud.renderContainerScreen(uiRenderer, world, assets, recipes, mx, my, window.leftClicked, window.rightClicked);

            if (hud.activeScreen == GUI_PAUSE) {
                uiRenderer.drawGradientRect(0, 0, (float)uiRenderer.scaledW, (float)uiRenderer.scaledH, 0xC0101010, 0xD0101010);
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
                uiRenderer.drawGradientRect(0, 0, (float)uiRenderer.scaledW, (float)uiRenderer.scaledH, 0x60500000, 0x80300000);
                float cx = uiRenderer.scaledW * 0.5f, cy = uiRenderer.scaledH * 0.5f;
                uiRenderer.drawCenteredString(assets, "Game over!", cx, cy - 32.0f, 0xFFFFFFFF);
                if (uiRenderer.drawClassicButton(assets, "Respawn", cx - 100.0f, cy, 200.0f, 20.0f, mx, my, window.leftClicked)) {
                    player.x = player.prevX = 0.5f;
                    player.z = player.prevZ = 0.5f;
                    player.y = player.prevY = (float)world.getTopSolidY(0, 0) + 1.5f;
                    player.health = 20.0f;
                    hud.activeScreen = GUI_NONE;
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