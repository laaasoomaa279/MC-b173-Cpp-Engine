#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "assets/recipes_b173.hpp"
#include "world/world.hpp"
#include "gameplay/physics.hpp"
#include "gameplay/combat_nether.hpp"
#include "gameplay/redstone_pistons.hpp"
#include "entities/entities_ai.hpp"
#include "ui/ui_hud_containers.hpp"
#include "render/sky_weather.hpp"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

// ============================================================================
// CANONICAL FIRST-PERSON CONTROLLER & INTERACTION SUBSYSTEM (SPEC-2011)
// ============================================================================
// Technical Subsystem Architecture:
// 1. Dual-Phase First-Person View Frustum:
//    - The first-person hand and held inventory item utilize an independent
//      depth range and projection matrix with a dedicated FOV (70.0 degrees on
//      land, 60.0 degrees submerged).
//    - Depth Range & Near-Plane Clipping Avoidance:
//        The local model transformation strictly constrains all vertices to
//        Z in [-0.35F, -1.20F], ensuring no geometry ever breaches the near
//        plane (nearZ = 0.05F) to eliminate near-clipping planar distortion.
// 2. Continuous Locomotion Bobbing & Damage Roll:
//    - Horizontal & Vertical Sway:
//        bobX = -cos(walkDist * PI) * cameraYaw * 0.5F
//        bobY =  abs(sin(walkDist * PI)) * cameraYaw * 0.5F
//        bobRoll = sin(walkDist * PI) * cameraYaw * 3.0F
//        bobPitch = abs(cos(walkDist * PI - 0.2F) * cameraYaw) * 5.0F
//    - Hurt Roll Impulse:
//        ht = (hurtTime - alpha) / 10.0F
//        hurtRoll = sin(ht * ht * PI) * 14.0F
// 3. Sub-Voxel Primary & Secondary Kinetic Dispatches:
//    - Primary Dispatch: Manages sub-voxel hit testing, kinetic tool wear,
//      continuous block harvest timing (hardness / 30.0F vs 100.0F), particle
//      fragment dispersal, and entity melee combat.
//    - Secondary Dispatch: Handles fluid bucket collection/dispersion, agricultural
//      tilling, bonemeal acceleration, directional block placement, container
//      interfaces (Crafting, Furnace, Chest), and ballistic projectile release.
// 4. Command Parser & State Mutation Engine:
//    - Autonomous tokenization and execution of diagnostic console commands
//      (/fly, /heal, /kill, /give, /tp, /time, /weather, /summon, /clear, /seed).
// ============================================================================

template<typename QueueType>
inline void enqueueBlockAndNeighbors(QueueType& meshQueue, int wx, int wz) {
    int32_t cx = wx >> 4;
    int32_t cz = wz >> 4;
    meshQueue.push({ cx, cz });
    if ((wx & 15) == 0)  meshQueue.push({ cx - 1, cz });
    if ((wx & 15) == 15) meshQueue.push({ cx + 1, cz });
    if ((wz & 15) == 0)  meshQueue.push({ cx, cz - 1 });
    if ((wz & 15) == 15) meshQueue.push({ cx, cz + 1 });
}

class FirstPersonAndOverlayRenderer {
public:
    GLuint wireShader = 0, wireVao = 0, wireVbo = 0;
    GLuint texShader  = 0, texVao  = 0, texVbo  = 0;

    float    equippedProgress      = 1.0f;
    float    prevEquippedProgress  = 1.0f;
    float    prevSwingProgress     = 0.0f;
    float    lastTickSwingProgress = 0.0f;
    int      lastEquippedSlot      = -1;
    uint16_t lastEquippedId        = 0;

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
            void main() { FragColor = vec4(0.0, 0.0, 0.0, 0.42); }
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

    void tickEquippedItem(const PlayerPhysics& player, int currentSlot, const ItemStack& held) {
        prevEquippedProgress  = equippedProgress;
        prevSwingProgress     = lastTickSwingProgress;
        if (prevSwingProgress > player.swingProgress) {
            prevSwingProgress = 0.0f;
        }
        lastTickSwingProgress = player.swingProgress;

        bool sameItem = (currentSlot == lastEquippedSlot) && (held.id == lastEquippedId);
        float target  = sameItem ? 1.0f : 0.0f;
        equippedProgress += std::clamp(target - equippedProgress, -0.4f, 0.4f);

        if (equippedProgress < 0.1f) {
            lastEquippedSlot = currentSlot;
            lastEquippedId   = held.id;
        }
    }

    void drawSelectionBox(const WorldMap& world, const Mat4& viewProj, const Vec3& eyePos, int bx, int by, int bz) {
        if (!wireShader) return;
        uint16_t st = world.getBlockAt(bx, by, bz);
        const BlockDef& def = world.registry.get(st);
        uint8_t id = static_cast<uint8_t>((st >> 4) & 0xFF);

        float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
        float maxX = 1.0f, maxY = 1.0f, maxZ = 1.0f;

        if (def.shape == SHAPE_SLAB && id != 63) {
            maxY = 0.5f;
        } else if (def.shape == SHAPE_SNOW_LAYER) {
            maxY = std::min(1.0f, static_cast<float>((st & 0x07) + 1) * 0.125f);
        } else if (def.shape == SHAPE_BED) {
            maxY = 0.5625f;
        } else if (def.shape == SHAPE_CAKE) {
            int eaten = std::min(6, static_cast<int>(st & 0x07));
            minX = static_cast<float>(1 + eaten * 2) * 0.0625f;
            maxX = 0.9375f; maxY = 0.5f; minZ = 0.0625f; maxZ = 0.9375f;
        } else if (def.shape == SHAPE_REDSTONE_WIRE || def.shape == SHAPE_RAIL || def.shape == SHAPE_PRESSURE_PLATE) {
            maxY = 0.0625f;
        } else if (def.shape == SHAPE_REPEATER) {
            maxY = 0.125f;
        } else if (def.shape == SHAPE_CACTUS) {
            minX = 0.0625f; maxX = 0.9375f; minZ = 0.0625f; maxZ = 0.9375f;
        } else if (def.shape != SHAPE_CUBE && def.shape != SHAPE_STAIR && def.shape != SHAPE_PISTON) {
            SubBoxList subs;
            world.registry.getSubBoxes(st, bx, by, bz, [&](int qx, int qy, int qz) {
                return world.getBlockAt(qx, qy, qz);
            }, subs);
            if (subs.count > 0) {
                minX = subs.boxes[0].minX * 0.0625f; minY = subs.boxes[0].minY * 0.0625f; minZ = subs.boxes[0].minZ * 0.0625f;
                maxX = subs.boxes[0].maxX * 0.0625f; maxY = subs.boxes[0].maxY * 0.0625f; maxZ = subs.boxes[0].maxZ * 0.0625f;
                for (int i = 1; i < subs.count; ++i) {
                    minX = std::min(minX, subs.boxes[i].minX * 0.0625f);
                    minY = std::min(minY, subs.boxes[i].minY * 0.0625f);
                    minZ = std::min(minZ, subs.boxes[i].minZ * 0.0625f);
                    maxX = std::max(maxX, subs.boxes[i].maxX * 0.0625f);
                    maxY = std::max(maxY, subs.boxes[i].maxY * 0.0625f);
                    maxZ = std::max(maxZ, subs.boxes[i].maxZ * 0.0625f);
                }
            }
        }

        const float e = 0.002f;
        float x0 = (bx + minX - e) - eyePos.x, x1 = (bx + maxX + e) - eyePos.x;
        float y0 = (by + minY - e) - eyePos.y, y1 = (by + maxY + e) - eyePos.y;
        float z0 = (bz + minZ - e) - eyePos.z, z1 = (bz + maxZ + e) - eyePos.z;

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

    void drawBlockBreakCracks(const Mat4& viewProj, const BetaAssetManager& assets,
                              const Vec3& eyePos, int bx, int by, int bz, float breakProgress) {
        if (!texShader || !assets.terrainTex || breakProgress <= 0.01f) return;
        int stage = std::clamp(static_cast<int>(breakProgress * 10.0f), 0, 9);
        uint16_t spr = static_cast<uint16_t>(SPR_DESTROY_0 + stage);

        float u0 = (spr % 16) / 16.0f + 0.001f;
        float v0 = (spr / 16) / 16.0f + 0.001f;
        float u1 = (spr % 16 + 1) / 16.0f - 0.001f;
        float v1 = (spr / 16 + 1) / 16.0f - 0.001f;

        float x0 = bx - eyePos.x, x1 = (bx + 1.0f) - eyePos.x;
        float y0 = by - eyePos.y, y1 = (by + 1.0f) - eyePos.y;
        float z0 = bz - eyePos.z, z1 = (bz + 1.0f) - eyePos.z;

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
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);
        glDepthMask(GL_FALSE);
        glUseProgram(texShader);
        glUniformMatrix4fv(glGetUniformLocation(texShader, "u_mvp"), 1, GL_FALSE, viewProj.m);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, assets.terrainTex);
        submitTexVerts(v);
        glDepthMask(GL_TRUE);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_BLEND);
    }

    void drawFirstPersonFireOverlay(const BetaAssetManager& assets, float totalTimeSec) {
        if (!texShader || !assets.terrainTex) return;
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(texShader);
        Mat4 ortho = Mat4::ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
        glUniformMatrix4fv(glGetUniformLocation(texShader, "u_mvp"), 1, GL_FALSE, ortho.m);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, assets.terrainTex);

        uint16_t spr = SPR_FIRE_0;
        float u0 = (spr % 16) / 16.0f + 0.001f;
        float v0 = (spr / 16) / 16.0f + 0.001f;
        float u1 = (spr % 16 + 1) / 16.0f - 0.001f;
        float v1 = (spr / 16 + 1) / 16.0f - 0.001f;
        float bob = std::sin(totalTimeSec * 22.0f) * 0.03f;

        std::vector<TexVert> v = {
            {-0.95f, -1.05f + bob, 0.0f, u0, v1, 1, 1, 1, 0.85f},
            { 0.15f, -1.05f + bob, 0.0f, u1, v1, 1, 1, 1, 0.85f},
            { 0.15f, -0.05f + bob, 0.0f, u1, v0, 1, 1, 1, 0.85f},
            {-0.95f, -1.05f + bob, 0.0f, u0, v1, 1, 1, 1, 0.85f},
            { 0.15f, -0.05f + bob, 0.0f, u1, v0, 1, 1, 1, 0.85f},
            {-0.95f, -0.05f + bob, 0.0f, u0, v0, 1, 1, 1, 0.85f},

            {-0.15f, -1.05f - bob, 0.0f, u1, v1, 1, 1, 1, 0.85f},
            { 0.95f, -1.05f - bob, 0.0f, u0, v1, 1, 1, 1, 0.85f},
            { 0.95f, -0.05f - bob, 0.0f, u0, v0, 1, 1, 1, 0.85f},
            {-0.15f, -1.05f - bob, 0.0f, u1, v1, 1, 1, 1, 0.85f},
            { 0.95f, -0.05f - bob, 0.0f, u0, v0, 1, 1, 1, 0.85f},
            {-0.15f, -0.05f - bob, 0.0f, u1, v0, 1, 1, 1, 0.85f}
        };
        submitTexVerts(v);

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }

    void drawFirstPersonHeld(const WorldMap& world, const BetaAssetManager& assets,
                             const PlayerPhysics& player, const ItemStack& held,
                             float aspect, float alpha, float skyBrightness) {
        if (!texShader) return;

        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        const float PI = 3.14159265f;
        float equip = prevEquippedProgress + (equippedProgress - prevEquippedProgress) * alpha;
        float swing = prevSwingProgress + (player.swingProgress - prevSwingProgress) * alpha;
        float wd    = player.prevWalkDist + (player.walkDist - player.prevWalkDist) * alpha;
        float bob   = player.prevCameraYaw + (player.cameraYaw - player.prevCameraYaw) * alpha;

        float bobX     = -std::cos(wd * PI) * bob * 0.5f;
        float bobY     =  std::abs(std::sin(wd * PI)) * bob * 0.5f;
        float bobRoll  =  std::sin(wd * PI) * bob * 3.0f;
        float bobPitch =  std::abs(std::cos(wd * PI - 0.2f) * bob) * 5.0f;

        float hurtRoll = 0.0f;
        if (player.hurtTime > 0) {
            float ht = (static_cast<float>(player.hurtTime) - alpha) / 10.0f;
            hurtRoll = std::sin(ht * ht * PI) * 14.0f;
        }

        float f1 = std::sin(swing * PI);
        float f2 = std::sin(std::sqrt(swing) * PI);
        float f3 = std::sin(swing * swing * PI);

        int bx = static_cast<int>(std::floor(player.x));
        int by = static_cast<int>(std::floor(player.y + 1.0f));
        int bz = static_cast<int>(std::floor(player.z));
        float sky = (world.getSkyLightAt(bx, by, bz) / 15.0f) * skyBrightness;
        float blk = world.getBlockLightAt(bx, by, bz) / 15.0f;
        float lightLevel = std::clamp(std::max(sky, blk), 0.0f, 1.0f);
        float lf = 1.0f - lightLevel;
        float light = ((1.0f - lf) / (lf * 3.0f + 1.0f)) * 0.95f + 0.05f;

        float handFov = player.eyesInWater ? 60.0f : 70.0f;
        Mat4 proj = Mat4::perspective(handFov, aspect, 0.05f, 10.0f) *
                    Mat4::rotateZ(bobRoll + hurtRoll) *
                    Mat4::rotateX(bobPitch);

        glUseProgram(texShader);
        glActiveTexture(GL_TEXTURE0);

        if (!held.isEmpty()) {
            if (held.id < 256) {
                uint16_t rawSt = static_cast<uint16_t>((held.id << 4) | (held.damage & 0x0F));
                const BlockDef& def = world.registry.get(rawSt);
                if (def.shape == SHAPE_CUBE || def.shape == SHAPE_SLAB || def.shape == SHAPE_STAIR || def.shape == SHAPE_PISTON) {
                    Mat4 blockMat = proj *
                                    Mat4::translate(0.56f + bobX - f2 * 0.4f,
                                                    -0.52f - bobY - (1.0f - equip) * 0.6f + f1 * 0.2f,
                                                    -0.72f - f1 * 0.2f) *
                                    Mat4::rotateY(45.0f) *
                                    Mat4::rotateY(-f3 * 20.0f) *
                                    Mat4::rotateZ(-f2 * 20.0f) *
                                    Mat4::rotateX(f2 * 80.0f) *
                                    Mat4::scale(0.40f, (def.shape == SHAPE_SLAB) ? 0.20f : 0.40f, 0.40f);

                    glUniformMatrix4fv(glGetUniformLocation(texShader, "u_mvp"), 1, GL_FALSE, blockMat.m);
                    glBindTexture(GL_TEXTURE_2D, assets.terrainTex);

                    float tr = light, tg = light, tb = light;
                    if (def.tintMode == TINT_GRASS || def.tintMode == TINT_FOLIAGE) {
                        float br, bg, bb;
                        assets.sampleBiomeColor(0.7f, 0.7f, def.tintMode == TINT_FOLIAGE, br, bg, bb);
                        tr *= br; tg *= bg; tb *= bb;
                    }

                    std::vector<TexVert> v;
                    v.reserve(36);
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
                    float fB[4][3]={{-h,-h,-h},{h,-h,-h},{h,-h,h},{-h,-h,h}};
                    float fS[4][3]={{-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}};
                    float fN[4][3]={{h,-h,-h},{-h,-h,-h},{-h,h,-h},{h,h,-h}};
                    float fE[4][3]={{h,-h,h},{h,-h,-h},{h,h,-h},{h,h,h}};
                    float fW[4][3]={{-h,-h,-h},{-h,-h,h},{-h,h,h},{-h,h,-h}};

                    addBlockFace(world.registry.resolveFaceTexture(rawSt, FACE_TOP),    fT, tr, tg, tb);
                    addBlockFace(world.registry.resolveFaceTexture(rawSt, FACE_BOTTOM), fB, light * 0.5f, light * 0.5f, light * 0.5f);
                    addBlockFace(world.registry.resolveFaceTexture(rawSt, FACE_SOUTH),  fS, light * 0.8f, light * 0.8f, light * 0.8f);
                    addBlockFace(world.registry.resolveFaceTexture(rawSt, FACE_NORTH),  fN, light * 0.8f, light * 0.8f, light * 0.8f);
                    addBlockFace(world.registry.resolveFaceTexture(rawSt, FACE_EAST),   fE, light * 0.6f, light * 0.6f, light * 0.6f);
                    addBlockFace(world.registry.resolveFaceTexture(rawSt, FACE_WEST),   fW, light * 0.6f, light * 0.6f, light * 0.6f);
                    submitTexVerts(v);
                    return;
                }
            }

            GLuint tex = (held.id < 256) ? assets.terrainTex : assets.itemsTex;
            uint16_t spr = (held.id < 256)
                ? world.registry.resolveFaceTexture(static_cast<uint16_t>((held.id << 4) | (held.damage & 0x0F)), FACE_NORTH)
                : assets.getItemDef(held.id).spriteIdx;

            Mat4 itemMat = proj *
                           Mat4::translate(0.52f + bobX - f2 * 0.38f,
                                           -0.56f - bobY - (1.0f - equip) * 0.6f + f1 * 0.18f,
                                           -0.70f - f1 * 0.22f) *
                           Mat4::rotateY(72.0f) *
                           Mat4::rotateZ(-14.0f) *
                           Mat4::rotateY(-f3 * 22.0f) *
                           Mat4::rotateZ(-f2 * 65.0f) *
                           Mat4::rotateX(-f2 * 18.0f) *
                           Mat4::scale(0.62f, 0.62f, 0.62f);

            glUniformMatrix4fv(glGetUniformLocation(texShader, "u_mvp"), 1, GL_FALSE, itemMat.m);
            glBindTexture(GL_TEXTURE_2D, tex);

            float u0 = (spr % 16) / 16.0f + 0.0005f, v0 = (spr / 16) / 16.0f + 0.0005f;
            float u1 = (spr % 16 + 1) / 16.0f - 0.0005f, v1 = (spr / 16 + 1) / 16.0f - 0.0005f;
            const float d = 0.03125f;

            std::vector<TexVert> v;
            v.reserve(12 + 16 * 24);

            TexVert fb[12] = {
                {0.0f, 0.0f,  d, u0,v1, light,light,light,1},
                {1.0f, 0.0f,  d, u1,v1, light,light,light,1},
                {1.0f, 1.0f,  d, u1,v0, light,light,light,1},
                {0.0f, 0.0f,  d, u0,v1, light,light,light,1},
                {1.0f, 1.0f,  d, u1,v0, light,light,light,1},
                {0.0f, 1.0f,  d, u0,v0, light,light,light,1},

                {1.0f, 0.0f, -d, u1,v1, light*0.85f,light*0.85f,light*0.85f,1},
                {0.0f, 0.0f, -d, u0,v1, light*0.85f,light*0.85f,light*0.85f,1},
                {0.0f, 1.0f, -d, u0,v0, light*0.85f,light*0.85f,light*0.85f,1},
                {1.0f, 0.0f, -d, u1,v1, light*0.85f,light*0.85f,light*0.85f,1},
                {0.0f, 1.0f, -d, u0,v0, light*0.85f,light*0.85f,light*0.85f,1},
                {1.0f, 1.0f, -d, u1,v0, light*0.85f,light*0.85f,light*0.85f,1}
            };
            v.insert(v.end(), fb, fb + 12);

            for (int k = 0; k < 16; ++k) {
                float f0  = static_cast<float>(k) / 16.0f;
                float f1p = static_cast<float>(k + 1) / 16.0f;
                float tu  = u0 + (u1 - u0) * (f0 + 0.03125f);
                float tv  = v1 - (v1 - v0) * (f0 + 0.03125f);
                float scX = light * 0.65f;
                float scY = light * 0.80f;

                TexVert eL[6] = {
                    {f0, 0.0f, -d, tu, v1, scX,scX,scX,1}, {f0, 0.0f,  d, tu, v1, scX,scX,scX,1}, {f0, 1.0f,  d, tu, v0, scX,scX,scX,1},
                    {f0, 0.0f, -d, tu, v1, scX,scX,scX,1}, {f0, 1.0f,  d, tu, v0, scX,scX,scX,1}, {f0, 1.0f, -d, tu, v0, scX,scX,scX,1}
                };
                TexVert eR[6] = {
                    {f1p, 0.0f,  d, tu, v1, scX,scX,scX,1}, {f1p, 0.0f, -d, tu, v1, scX,scX,scX,1}, {f1p, 1.0f, -d, tu, v0, scX,scX,scX,1},
                    {f1p, 0.0f,  d, tu, v1, scX,scX,scX,1}, {f1p, 1.0f, -d, tu, v0, scX,scX,scX,1}, {f1p, 1.0f,  d, tu, v0, scX,scX,scX,1}
                };
                TexVert eT[6] = {
                    {0.0f, f1p,  d, u0, tv, scY,scY,scY,1}, {1.0f, f1p,  d, u1, tv, scY,scY,scY,1}, {1.0f, f1p, -d, u1, tv, scY,scY,scY,1},
                    {0.0f, f1p,  d, u0, tv, scY,scY,scY,1}, {1.0f, f1p, -d, u1, tv, scY,scY,scY,1}, {0.0f, f1p, -d, u0, tv, scY,scY,scY,1}
                };
                TexVert eB[6] = {
                    {0.0f, f0, -d, u0, tv, scY*0.6f,scY*0.6f,scY*0.6f,1}, {1.0f, f0, -d, u1, tv, scY*0.6f,scY*0.6f,scY*0.6f,1}, {1.0f, f0,  d, u1, tv, scY*0.6f,scY*0.6f,scY*0.6f,1},
                    {0.0f, f0, -d, u0, tv, scY*0.6f,scY*0.6f,scY*0.6f,1}, {1.0f, f0,  d, u1, tv, scY*0.6f,scY*0.6f,scY*0.6f,1}, {0.0f, f0,  d, u0, tv, scY*0.6f,scY*0.6f,scY*0.6f,1}
                };
                v.insert(v.end(), eL, eL + 6);
                v.insert(v.end(), eR, eR + 6);
                v.insert(v.end(), eT, eT + 6);
                v.insert(v.end(), eB, eB + 6);
            }
            submitTexVerts(v);
        } else if (assets.mobSkins[SKIN_PLAYER]) {
            // Canonical First-Person Arm: Angles forward-down into world [Z in -0.55 .. -1.10], eliminating near-plane paddle distortion
            Mat4 armMat = proj *
                          Mat4::translate(0.68f + bobX - f2 * 0.35f,
                                          -0.62f - bobY - (1.0f - equip) * 0.6f + std::sin(swing * PI * 2.0f) * 0.16f,
                                          -0.65f - f1 * 0.28f) *
                          Mat4::rotateY(36.0f) *
                          Mat4::rotateZ(-f2 * 20.0f) *
                          Mat4::rotateY(-f3 * 20.0f) *
                          Mat4::rotateX(f2 * 72.0f) *
                          Mat4::rotateZ(-16.0f) *
                          Mat4::rotateX(-58.0f) * // Rotates arm forward into -Z, well past near clipping plane (0.05F)
                          Mat4::scale(0.25f, 0.75f, 0.25f);

            glUniformMatrix4fv(glGetUniformLocation(texShader, "u_mvp"), 1, GL_FALSE, armMat.m);
            glBindTexture(GL_TEXTURE_2D, assets.mobSkins[SKIN_PLAYER]);

            std::vector<TexVert> v;
            v.reserve(36);
            auto addArmFace = [&](const float p[4][3], int pxU0, int pxV0, int pxU1, int pxV1, float shade) {
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

            float hw = 0.5f;
            float fS[4][3] = { {-hw, -1.0f,  hw}, { hw, -1.0f,  hw}, { hw, 0.0f,  hw}, {-hw, 0.0f,  hw} }; // Front (+Z)
            float fN[4][3] = { { hw, -1.0f, -hw}, {-hw, -1.0f, -hw}, {-hw, 0.0f, -hw}, { hw, 0.0f, -hw} }; // Back (-Z)
            float fE[4][3] = { { hw, -1.0f,  hw}, { hw, -1.0f, -hw}, { hw, 0.0f, -hw}, { hw, 0.0f,  hw} }; // Outer (+X)
            float fW[4][3] = { {-hw, -1.0f, -hw}, {-hw, -1.0f,  hw}, {-hw, 0.0f,  hw}, {-hw, 0.0f, -hw} }; // Inner (-X)
            float fT[4][3] = { {-hw,  0.0f,  hw}, { hw,  0.0f,  hw}, { hw, 0.0f, -hw}, {-hw, 0.0f, -hw} }; // Shoulder (Y=0)
            float fB[4][3] = { {-hw, -1.0f, -hw}, { hw, -1.0f, -hw}, { hw, -1.0f,  hw}, {-hw, -1.0f,  hw} }; // Fist (Y=-1)

            addArmFace(fS, 44, 20, 48, 32, 0.85f);
            addArmFace(fN, 52, 20, 56, 32, 1.00f);
            addArmFace(fE, 40, 20, 44, 32, 0.90f);
            addArmFace(fW, 48, 20, 52, 32, 0.75f);
            addArmFace(fT, 44, 16, 48, 20, 0.80f);
            addArmFace(fB, 48, 16, 52, 20, 0.95f);
            submitTexVerts(v);
        }
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
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    }
};

class BetaInteractionController {
public:
    static void damageTool(ItemStack& held, const BetaAssetManager& assets, uint16_t amount) {
        if (held.isEmpty() || held.id < 256 || amount == 0) return;
        const ItemDef& def = assets.getItemDef(held.id);
        if (def.maxDamage == 0) return;

        uint32_t nextDmg = static_cast<uint32_t>(held.damage) + static_cast<uint32_t>(amount);
        if (nextDmg >= def.maxDamage) held.clear();
        else held.damage = static_cast<uint16_t>(nextDmg);
    }

    static RaycastHit raycastFluids(const WorldMap& world, const Vec3& origin, const Vec3& dir, float maxDist) {
        RaycastHit out{};
        int bx = static_cast<int>(std::floor(origin.x));
        int by = static_cast<int>(std::floor(origin.y));
        int bz = static_cast<int>(std::floor(origin.z));

        int stepX = (dir.x > 0.0f) ? 1 : ((dir.x < 0.0f) ? -1 : 0);
        int stepY = (dir.y > 0.0f) ? 1 : ((dir.y < 0.0f) ? -1 : 0);
        int stepZ = (dir.z > 0.0f) ? 1 : ((dir.z < 0.0f) ? -1 : 0);

        float tDeltaX = (stepX != 0) ? std::abs(1.0f / dir.x) : 1e30f;
        float tDeltaY = (stepY != 0) ? std::abs(1.0f / dir.y) : 1e30f;
        float tDeltaZ = (stepZ != 0) ? std::abs(1.0f / dir.z) : 1e30f;

        float tMaxX = (stepX > 0) ? ((bx + 1.0f - origin.x) * tDeltaX) : ((origin.x - bx) * tDeltaX);
        float tMaxY = (stepY > 0) ? ((by + 1.0f - origin.y) * tDeltaY) : ((origin.y - by) * tDeltaY);
        float tMaxZ = (stepZ > 0) ? ((bz + 1.0f - origin.z) * tDeltaZ) : ((origin.z - bz) * tDeltaZ);

        int nx = 0, ny = 0, nz = 0;
        float dist = 0.0f;
        for (int i = 0; i < 64 && dist <= maxDist; ++i) {
            if (by >= 0 && by < CHUNK_HEIGHT) {
                uint8_t id = static_cast<uint8_t>((world.getBlockAt(bx, by, bz) >> 4) & 0xFF);
                if (id != 0) {
                    out.hit = true;
                    out.bx = bx; out.by = by; out.bz = bz;
                    out.nx = nx; out.ny = ny; out.nz = nz;
                    out.distance = dist;
                    return out;
                }
            }
            if (tMaxX < tMaxY && tMaxX < tMaxZ) {
                dist = tMaxX; tMaxX += tDeltaX; bx += stepX; nx = -stepX; ny = 0; nz = 0;
            } else if (tMaxY < tMaxZ) {
                dist = tMaxY; tMaxY += tDeltaY; by += stepY; nx = 0; ny = -stepY; nz = 0;
            } else {
                dist = tMaxZ; tMaxZ += tDeltaZ; bz += stepZ; nx = 0; ny = 0; nz = -stepZ;
            }
        }
        return out;
    }

    template<typename QueueType>
    static void processPrimaryAction(WorldMap& world, const BetaAssetManager& assets,
                                     PlayerPhysics& player, BetaInventoryAndHud& hud,
                                     BetaEntityManager& entities, BetaCombatAndNetherSystem& combat,
                                     BetaRedstonePistonEngine& redstone, AudioEngine& audio,
                                     const Vec3& eyePos, const Vec3& lookDir, const RaycastHit& hit,
                                     bool leftClicked, bool leftDown, float dt, QueueType& meshQueue) {
        ItemStack& held = hud.getHeldStack();

        if (leftClicked) {
            if (entities.performPlayerMeleeAttack(player, assets, held.id, eyePos, lookDir, &audio, &combat)) {
                if (!player.flying && held.id >= 256) {
                    ToolCategory tc = assets.getItemDef(held.id).toolType;
                    damageTool(held, assets, (tc == TOOL_SWORD) ? 1 : ((tc != TOOL_NONE) ? 2 : 0));
                }
                return;
            }
            if (hit.hit && ((world.getBlockAt(hit.bx, hit.by, hit.bz) >> 4) & 0xFF) == 46 && held.id == ITEM_FLINT_AND_STEEL) {
                world.setBlockAt(hit.bx, hit.by, hit.bz, 0, true);
                redstone.primedTnts.push_back({ hit.bx + 0.5f, static_cast<float>(hit.by), hit.bz + 0.5f, 0.0f, 0.20f, 0.0f, 80 });
                enqueueBlockAndNeighbors(meshQueue, hit.bx, hit.bz);
                if (!player.flying) damageTool(held, assets, 1);
                return;
            }
        }

        if (!leftDown || !hit.hit) {
            player.breakProgress = 0.0f;
            return;
        }

        player.triggerSwing();
        if (player.flying) {
            if (player.blockHitDelay == 0) {
                uint16_t oldSt = world.getBlockAt(hit.bx, hit.by, hit.bz);
                entities.spawnBlockBreakParticles(world, hit.bx, hit.by, hit.bz, oldSt);
                world.setBlockAt(hit.bx, hit.by, hit.bz, 0, true);
                redstone.onBlockChanged(world, hit.bx, hit.by, hit.bz, meshQueue);
                enqueueBlockAndNeighbors(meshQueue, hit.bx, hit.bz);
                player.blockHitDelay = 5;
            }
            return;
        }

        if (player.blockHitDelay > 0) return;

        if (hit.bx != player.miningX || hit.by != player.miningY || hit.bz != player.miningZ) {
            player.miningX = hit.bx;
            player.miningY = hit.by;
            player.miningZ = hit.bz;
            player.breakProgress = 0.0f;
        }

        player.breakProgress += player.computeBlockDamagePerTick(world, assets, hit.bx, hit.by, hit.bz, held.id) * (dt * 20.0f);
        if (player.breakProgress >= 1.0f) {
            uint16_t brokenSt = world.getBlockAt(hit.bx, hit.by, hit.bz);
            const BlockDef& bDef = world.registry.get(brokenSt);

            if (!assets.digSounds[bDef.soundMat].samples.empty()) {
                audio.play3D(assets.digSounds[bDef.soundMat], 1.0f, 0.8f,
                             {hit.bx + 0.5f, hit.by + 0.5f, hit.bz + 0.5f}, eyePos, player.yaw, true);
            }

            uint16_t dropId = 0, dropMeta = 0;
            uint8_t dropCnt = 0;
            PlayerPhysics::resolveBlockDrops(world, assets, brokenSt, held.id, static_cast<uint32_t>(world.worldTick), dropId, dropMeta, dropCnt);
            if (dropId != 0 && dropCnt > 0) {
                entities.spawnDroppedItem(dropId, dropMeta, dropCnt, hit.bx + 0.5f, hit.by + 0.5f, hit.bz + 0.5f);
            }
            entities.spawnBlockBreakParticles(world, hit.bx, hit.by, hit.bz, brokenSt);

            uint8_t brokenId = static_cast<uint8_t>((brokenSt >> 4) & 0xFF);
            if ((brokenId == 64 || brokenId == 71) && (brokenSt & 0x08) != 0) {
                world.setBlockAt(hit.bx, hit.by - 1, hit.bz, 0, false);
            }

            world.setBlockAt(hit.bx, hit.by, hit.bz, 0, true);
            redstone.onBlockChanged(world, hit.bx, hit.by, hit.bz, meshQueue);
            enqueueBlockAndNeighbors(meshQueue, hit.bx, hit.bz);

            if (bDef.hardness > 0.0f && held.id >= 256) {
                ToolCategory tc = assets.getItemDef(held.id).toolType;
                damageTool(held, assets, (tc == TOOL_SWORD) ? 2 : ((tc != TOOL_NONE) ? 1 : 0));
            }

            player.breakProgress = 0.0f;
            player.blockHitDelay = 5;
        }
    }

    template<typename QueueType>
    static void processSecondaryAction(WorldMap& world, const BetaAssetManager& assets,
                                       const BetaRecipeRegistry& recipes, PlatformWindow& window,
                                       PlayerPhysics& player, BetaInventoryAndHud& hud,
                                       BetaEntityManager& entities, BetaCombatAndNetherSystem& combat,
                                       BetaRedstonePistonEngine& redstone, AudioEngine& audio,
                                       const Vec3& eyePos, const Vec3& lookDir, const RaycastHit& hit,
                                       QueueType& meshQueue) {
        ItemStack& held = hud.getHeldStack();
        const ItemDef& itDef = assets.getItemDef(held.id);

        if (held.id == ITEM_BUCKET) {
            for (const auto& m : entities.mobs) {
                if (m.type == MOB_COW && m.health > 0.0f) {
                    float dx = m.x - eyePos.x, dy = (m.y + 0.7f) - eyePos.y, dz = m.z - eyePos.z;
                    if (dx * dx + dy * dy + dz * dz < 14.0f) {
                        player.triggerSwing();
                        if (held.count == 1) held = { ITEM_MILK_BUCKET, 0, 1 };
                        else { held.count--; hud.giveItem(assets, ITEM_MILK_BUCKET, 0, 1); }
                        return;
                    }
                }
            }
            RaycastHit fHit = raycastFluids(world, eyePos, lookDir, 4.5f);
            if (fHit.hit) {
                uint16_t fSt = world.getBlockAt(fHit.bx, fHit.by, fHit.bz);
                uint8_t fId  = static_cast<uint8_t>((fSt >> 4) & 0xFF);
                uint8_t fMet = static_cast<uint8_t>(fSt & 0x0F);
                if (fId == 8 || fId == 9 || fId == 10 || fId == 11) {
                    int sx = fHit.bx, sy = fHit.by, sz = fHit.bz;
                    if (fMet != 0 && fId != 9 && fId != 11) {
                        uint16_t ab = world.getBlockAt(sx, sy + 1, sz);
                        uint8_t abId = static_cast<uint8_t>((ab >> 4) & 0xFF);
                        if ((abId == 9 || (abId == 8 && (ab & 0x0F) == 0)) || (abId == 11 || (abId == 10 && (ab & 0x0F) == 0))) {
                            sy++; fId = abId; fMet = static_cast<uint8_t>(ab & 0x0F);
                        }
                    }
                    if (fId == 9 || fId == 11 || fMet == 0) {
                        uint16_t filled = (fId == 8 || fId == 9) ? ITEM_WATER_BUCKET : ITEM_LAVA_BUCKET;
                        world.setBlockAt(sx, sy, sz, 0, true);
                        enqueueBlockAndNeighbors(meshQueue, sx, sz);
                        player.triggerSwing();
                        if (held.count == 1) held = { filled, 0, 1 };
                        else { held.count--; hud.giveItem(assets, filled, 0, 1); }
                        return;
                    }
                }
            }
        }

        if ((held.id == ITEM_WATER_BUCKET || held.id == ITEM_LAVA_BUCKET) && hit.hit) {
            int px = hit.bx + hit.nx, py = hit.by + hit.ny, pz = hit.bz + hit.nz;
            uint16_t dstSt = world.getBlockAt(px, py, pz);
            if (dstSt == 0 || world.registry.get(dstSt).isReplaceable) {
                if (held.id == ITEM_WATER_BUCKET && world.dimension == -1) {
                    if (!assets.fizzSound.samples.empty()) audio.play2D(assets.fizzSound, 0.6f, 2.4f);
                } else {
                    uint8_t fluidId = (held.id == ITEM_WATER_BUCKET) ? 8 : 10;
                    world.setBlockAt(px, py, pz, static_cast<uint16_t>(fluidId << 4), true);
                    enqueueBlockAndNeighbors(meshQueue, px, pz);
                }
                player.triggerSwing();
                if (!player.flying) held = { ITEM_BUCKET, 0, 1 };
                return;
            }
        }

        if (held.id == ITEM_SHEARS) {
            for (auto& m : entities.mobs) {
                if (m.type == MOB_SHEEP && !m.sheared && m.health > 0.0f) {
                    float dx = m.x - eyePos.x, dy = (m.y + 0.7f) - eyePos.y, dz = m.z - eyePos.z;
                    if (dx * dx + dy * dy + dz * dz < 14.0f) {
                        m.sheared = true;
                        entities.spawnDroppedItem(35, m.woolColor, static_cast<uint8_t>(1 + (world.worldTick % 3)), m.x, m.y + 0.8f, m.z);
                        if (!player.flying) damageTool(held, assets, 1);
                        player.triggerSwing();
                        return;
                    }
                }
            }
        }

        if (hit.hit && !player.sneaking) {
            uint16_t targetSt = world.getBlockAt(hit.bx, hit.by, hit.bz);
            uint8_t targetId  = static_cast<uint8_t>((targetSt >> 4) & 0xFF);
            uint8_t targetMet = static_cast<uint8_t>(targetSt & 0x0F);

            if (targetId == 58) {
                hud.activeScreen = GUI_CRAFTING;
                hud.updateCraftingPreview(recipes);
                window.setMouseLock(false);
                return;
            }
            if (targetId == 61 || targetId == 62) {
                hud.openTilePos = { hit.bx, hit.by, hit.bz };
                hud.activeScreen = GUI_FURNACE;
                window.setMouseLock(false);
                return;
            }
            if (targetId == 54) {
                hud.openTilePos = { hit.bx, hit.by, hit.bz };
                hud.activeScreen = GUI_CHEST;
                window.setMouseLock(false);
                return;
            }
            if (targetId == 92) {
                if (player.health < 20.0f) {
                    player.heal(3.0f);
                    player.triggerSwing();
                    if (targetMet + 1 >= 6) world.setBlockAt(hit.bx, hit.by, hit.bz, 0, true);
                    else world.setBlockAt(hit.bx, hit.by, hit.bz, static_cast<uint16_t>((92 << 4) | (targetMet + 1)), false);
                    enqueueBlockAndNeighbors(meshQueue, hit.bx, hit.bz);
                }
                return;
            }
            if (targetId == 25) {
                uint8_t nextNote = static_cast<uint8_t>((targetMet + 1) % 25);
                world.setBlockAt(hit.bx, hit.by, hit.bz, static_cast<uint16_t>((25 << 4) | nextNote), false);
                if (!assets.clickSound.samples.empty()) {
                    audio.play2D(assets.clickSound, 0.8f, std::pow(2.0f, static_cast<float>(nextNote - 12) / 12.0f));
                }
                player.triggerSwing();
                return;
            }
            if (redstone.interactWithBlock(world, hit.bx, hit.by, hit.bz, meshQueue, &audio, &assets)) {
                player.triggerSwing();
                return;
            }
        }

        if (held.isEmpty()) return;

        uint16_t targetSt = hit.hit ? world.getBlockAt(hit.bx, hit.by, hit.bz) : 0;
        uint8_t targetId  = static_cast<uint8_t>((targetSt >> 4) & 0xFF);
        uint8_t targetMet = static_cast<uint8_t>(targetSt & 0x0F);

        if (held.id == ITEM_FLINT_AND_STEEL && hit.hit) {
            int px = hit.bx + hit.nx, py = hit.by + hit.ny, pz = hit.bz + hit.nz;
            if (targetId == 46) {
                world.setBlockAt(hit.bx, hit.by, hit.bz, 0, true);
                redstone.primedTnts.push_back({ hit.bx + 0.5f, static_cast<float>(hit.by), hit.bz + 0.5f, 0.0f, 0.20f, 0.0f, 80 });
                enqueueBlockAndNeighbors(meshQueue, hit.bx, hit.bz);
            } else if (!combat.tryActivateNetherPortal(world, px, py, pz, meshQueue)) {
                if (world.getBlockAt(px, py, pz) == 0) {
                    world.setBlockAt(px, py, pz, (51 << 4), true);
                    enqueueBlockAndNeighbors(meshQueue, px, pz);
                }
            }
            player.triggerSwing();
            if (!player.flying) damageTool(held, assets, 1);
            return;
        }
        if (hit.hit && (held.id >= ITEM_WOOD_HOE && held.id <= ITEM_GOLD_HOE) &&
            (targetId == 2 || targetId == 3) && world.getBlockAt(hit.bx, hit.by + 1, hit.bz) == 0) {
            world.setBlockAt(hit.bx, hit.by, hit.bz, (60 << 4), true);
            enqueueBlockAndNeighbors(meshQueue, hit.bx, hit.bz);
            if (!assets.stepSounds[SND_GRAVEL].samples.empty()) audio.play2D(assets.stepSounds[SND_GRAVEL], 0.8f, 0.9f);
            player.triggerSwing();
            if (!player.flying) damageTool(held, assets, 1);
            return;
        }
        if (hit.hit && held.id == ITEM_SEEDS && targetId == 60 && hit.ny == 1 && world.getBlockAt(hit.bx, hit.by + 1, hit.bz) == 0) {
            world.setBlockAt(hit.bx, hit.by + 1, hit.bz, (59 << 4), true);
            enqueueBlockAndNeighbors(meshQueue, hit.bx, hit.bz);
            player.triggerSwing();
            if (!player.flying && --held.count == 0) held.clear();
            return;
        }
        if (hit.hit && held.id == ITEM_DYE && held.damage == 15) {
            if (targetId == 59 && targetMet < 7) {
                world.setBlockAt(hit.bx, hit.by, hit.bz, static_cast<uint16_t>((59 << 4) | 7), false);
                enqueueBlockAndNeighbors(meshQueue, hit.bx, hit.bz);
                player.triggerSwing();
                if (!player.flying && --held.count == 0) held.clear();
                return;
            }
            if (targetId == 6) {
                uint8_t treeMeta = targetMet & 3;
                world.setBlockAt(hit.bx, hit.by, hit.bz, 0, false);
                for (int ty = 0; ty < 5; ++ty) world.setBlockAt(hit.bx, hit.by + ty, hit.bz, static_cast<uint16_t>((17 << 4) | treeMeta), false);
                for (int dy = 3; dy <= 5; ++dy) {
                    int r = (dy == 5) ? 1 : 2;
                    for (int dx = -r; dx <= r; ++dx)
                        for (int dz = -r; dz <= r; ++dz)
                            if (world.getBlockAt(hit.bx + dx, hit.by + dy, hit.bz + dz) == 0)
                                world.setBlockAt(hit.bx + dx, hit.by + dy, hit.bz + dz, static_cast<uint16_t>((18 << 4) | treeMeta), false);
                }
                enqueueBlockAndNeighbors(meshQueue, hit.bx, hit.bz);
                player.triggerSwing();
                if (!player.flying && --held.count == 0) held.clear();
                return;
            }
        }
        if (held.id >= 256 && itDef.healAmount > 0 && player.health < 20.0f) {
            player.heal(static_cast<float>(itDef.healAmount));
            player.triggerSwing();
            if (held.id == ITEM_MUSHROOM_STEW) held = { ITEM_BOWL, 0, 1 };
            else if (--held.count == 0) held.clear();
            return;
        }
        if (held.id == ITEM_BOW) {
            for (int s = 0; s < 36; ++s) {
                if (hud.mainSlots[s].id == ITEM_ARROW && hud.mainSlots[s].count > 0) {
                    if (!player.flying && --hud.mainSlots[s].count == 0) hud.mainSlots[s].clear();
                    combat.spawnProjectile(PROJ_ARROW, true, -1, eyePos, player.yaw, player.pitch, 1.55f);
                    player.triggerSwing();
                    if (!assets.bowSound.samples.empty()) audio.play2D(assets.bowSound, 1.0f, 1.0f);
                    return;
                }
            }
        }
        if (held.id == ITEM_SNOWBALL || held.id == ITEM_EGG) {
            combat.spawnProjectile((held.id == ITEM_EGG) ? PROJ_EGG : PROJ_SNOWBALL, true, -1, eyePos, player.yaw, player.pitch, 1.50f);
            player.triggerSwing();
            if (!assets.bowSound.samples.empty()) audio.play2D(assets.bowSound, 0.5f, 0.4f);
            if (!player.flying && --held.count == 0) held.clear();
            return;
        }

        if (!hit.hit) return;
        uint8_t placeId = (held.id < 256) ? static_cast<uint8_t>(held.id) : itDef.placesBlock;
        if (placeId == 0) return;

        int px = hit.bx + hit.nx, py = hit.by + hit.ny, pz = hit.bz + hit.nz;
        uint16_t existingSt = world.getBlockAt(px, py, pz);
        if (existingSt != 0 && !world.registry.get(existingSt).isReplaceable) return;

        AABB blkBox{ static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz), px + 1.0f, py + 1.0f, pz + 1.0f };
        AABB plrBox = AABB::fromFootPos(player.x, player.y, player.z, PlayerPhysics::HALF_WIDTH, PlayerPhysics::STAND_HEIGHT);
        if (world.registry.getById(placeId).isSolid && plrBox.intersects(blkBox)) return;

        uint8_t meta = static_cast<uint8_t>(held.damage & 0x0F);
        int dir4 = (static_cast<int>(std::floor(player.yaw / 90.0f + 0.5f))) & 3;

        if (placeId == 29 || placeId == 33) {
            if (std::abs(player.pitch) > 55.0f) meta = (player.pitch > 0.0f) ? 0 : 1;
            else { const uint8_t pMap[4] = { 4, 2, 5, 3 }; meta = pMap[dir4]; }
        } else if (placeId == 61 || placeId == 23) {
            const uint8_t fMap[4] = { 4, 2, 5, 3 }; meta = fMap[dir4];
        } else if (placeId == 86 || placeId == 91) {
            const uint8_t pMap[4] = { 1, 2, 3, 0 }; meta = pMap[dir4];
        } else if (placeId == 53 || placeId == 67) {
            const uint8_t sMap[4] = { 0, 2, 1, 3 }; meta = sMap[dir4];
        } else if (placeId == 93) {
            const uint8_t rMap[4] = { 1, 2, 3, 0 }; meta = rMap[dir4];
        } else if (placeId == 50 || placeId == 76 || placeId == 69) {
            meta = (hit.nx == 1) ? 1 : ((hit.nx == -1) ? 2 : ((hit.nz == 1) ? 3 : ((hit.nz == -1) ? 4 : 5)));
        } else if (placeId == 65 || placeId == 68) {
            meta = (hit.nz == -1) ? 2 : ((hit.nz == 1) ? 3 : ((hit.nx == -1) ? 4 : ((hit.nx == 1) ? 5 : 2)));
        } else if (placeId == 77) {
            meta = (hit.nx == 1) ? 1 : ((hit.nx == -1) ? 2 : ((hit.nz == 1) ? 3 : 4));
        } else if (placeId == 64 || placeId == 71) {
            meta = static_cast<uint8_t>(dir4 & 3);
        } else if (placeId == 26) {
            const uint8_t bMap[4] = { 3, 0, 1, 2 }; meta = bMap[dir4];
        }

        uint16_t placedState = static_cast<uint16_t>((placeId << 4) | meta);
        if (!world.canBlockStay(px, py, pz, placedState)) return;

        if (placeId == 64 || placeId == 71) {
            if (py + 1 >= CHUNK_HEIGHT || world.getBlockAt(px, py + 1, pz) != 0) return;
        } else if (placeId == 26) {
            const int bedOff[4][2] = { {0,1},{-1,0},{0,-1},{1,0} };
            if (world.getBlockAt(px + bedOff[meta & 3][0], py, pz + bedOff[meta & 3][1]) != 0) return;
        }

        world.setBlockAt(px, py, pz, placedState, true);
        if (placeId == 64 || placeId == 71) {
            world.setBlockAt(px, py + 1, pz, static_cast<uint16_t>((placeId << 4) | (meta | 0x08)), false);
        } else if (placeId == 26) {
            const int bedOff[4][2] = { {0,1},{-1,0},{0,-1},{1,0} };
            int hx = px + bedOff[meta & 3][0], hz = pz + bedOff[meta & 3][1];
            world.setBlockAt(hx, py, hz, static_cast<uint16_t>((26 << 4) | (meta | 0x08)), false);
            enqueueBlockAndNeighbors(meshQueue, hx, hz);
        }

        SoundMaterial pSnd = world.registry.getById(placeId).soundMat;
        if (!assets.stepSounds[pSnd].samples.empty()) {
            audio.play3D(assets.stepSounds[pSnd], 1.0f, 0.8f, {px + 0.5f, py + 0.5f, pz + 0.5f}, eyePos, player.yaw, true);
        }
        redstone.onBlockChanged(world, px, py, pz, meshQueue);
        enqueueBlockAndNeighbors(meshQueue, px, pz);
        player.triggerSwing();
        if (!player.flying && --held.count == 0) held.clear();
    }
};

inline void executeSlashCommand(const std::string& cmdLine, WorldMap& world, PlayerPhysics& player,
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
        player.fireTicks = 0;
        player.airSupply = 300;
        hud.addChatMessage("§aHealed to full 10 hearts!");
    } else if (cmd == "kill") {
        player.health = 0.0f;
    } else if (cmd == "clear") {
        for (int i = 0; i < 36; ++i) hud.mainSlots[i].clear();
        hud.addChatMessage("§eInventory cleared");
    } else if (cmd == "seed") {
        hud.addChatMessage("§eSeed: " + std::to_string(world.worldSeed));
    } else if (cmd == "time") {
        std::string sub; ss >> sub;
        if (sub == "day" || sub == "set") {
            int val = 1000;
            if (sub == "set") ss >> val;
            world.dayTime = static_cast<uint64_t>((val % 24000 + 24000) % 24000);
        } else if (sub == "night") {
            world.dayTime = 14000;
        } else if (sub == "sunset") {
            world.dayTime = 12000;
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
            player.vx = player.vy = player.vz = 0.0f;
            player.fallDistance = 0.0f;
            hud.addChatMessage("§aTeleported!");
        }
    } else if (cmd == "give") {
        int id = 1, count = 64, meta = 0;
        ss >> id;
        if (!(ss >> count)) count = 1;
        ss >> meta;
        hud.giveItem(assets, static_cast<uint16_t>(id), static_cast<uint16_t>(meta), static_cast<uint8_t>(std::clamp(count, 1, 64)));
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
        else if (mobName == "ghast")    mt = MOB_GHAST;
        entities.spawnMob(mt, player.x + 2.0f, player.y, player.z + 2.0f);
        hud.addChatMessage("§aSummoned " + mobName);
    } else {
        hud.addChatMessage("§cCommands: /fly /give /tp /time /weather /summon /heal /clear /seed");
    }
}