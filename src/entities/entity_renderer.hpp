#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "world/world.hpp"
#include "entities/entities_ai.hpp"

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================================
// Minecraft Beta 1.7.3 Batched Entity Renderer (1:1 Mojang RenderLiving.java)
// ============================================================================

struct EntityVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r, g, b, a;
};

class BetaEntityRenderer {
public:
    GLuint shader = 0;
    GLuint vao    = 0;
    GLuint vbo    = 0;

    std::vector<EntityVertex> skinBatches[SKIN_COUNT];
    std::vector<EntityVertex> terrainBatch;
    std::vector<EntityVertex> itemBatch;
    std::vector<EntityVertex> shadowBatch;

    void init() {
        const char* vSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_pos;
            layout(location = 1) in vec3 a_norm;
            layout(location = 2) in vec2 a_uv;
            layout(location = 3) in vec4 a_col;
            uniform mat4 u_viewProj;
            out vec2 v_uv;
            out vec4 v_col;
            void main() {
                v_uv = a_uv;
                vec3 l1 = normalize(vec3(0.2, 1.0, -0.7));
                vec3 l2 = normalize(vec3(-0.2, 1.0, 0.7));
                float shade = clamp(0.45 + 0.35 * max(dot(a_norm, l1), 0.0) + 0.35 * max(dot(a_norm, l2), 0.0), 0.35, 1.0);
                v_col = vec4(a_col.rgb * shade, a_col.a);
                gl_Position = u_viewProj * vec4(a_pos, 1.0);
            }
        )";
        const char* fSrc = R"(
            #version 330 core
            in vec2 v_uv;
            in vec4 v_col;
            uniform sampler2D u_tex;
            out vec4 FragColor;
            void main() {
                vec4 c = texture(u_tex, v_uv) * v_col;
                if (c.a < 0.02) discard;
                FragColor = c;
            }
        )";

        GLuint vs = compileShaderStage(GL_VERTEX_SHADER, vSrc, "entity.vert");
        GLuint fs = compileShaderStage(GL_FRAGMENT_SHADER, fSrc, "entity.frag");
        shader = glCreateProgram();
        glAttachShader(shader, vs);
        glAttachShader(shader, fs);
        glLinkProgram(shader);
        glDeleteShader(vs);
        glDeleteShader(fs);

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
    }

    void renderAll(const WorldMap& world, const BetaAssetManager& assets,
                   const BetaEntityManager& entities, const BetaCombatAndNetherSystem& combat,
                   const BetaRedstonePistonEngine& redstone,
                   const Mat4& viewProj, const Vec3& eyePos, float alpha, float totalTimeSec) {
        if (!shader) return;

        for (int s = 0; s < SKIN_COUNT; ++s) skinBatches[s].clear();
        terrainBatch.clear();
        itemBatch.clear();
        shadowBatch.clear();

        // 1. Batch all Mobs with separated Head/Body yaw, Death tilt & Ground Shadow
        for (const MobEntity& m : entities.mobs) {
            float wx = m.prevX + (m.x - m.prevX) * alpha;
            float wy = m.prevY + (m.y - m.prevY) * alpha;
            float wz = m.prevZ + (m.z - m.prevZ) * alpha;

            float mx = wx - eyePos.x;
            float my = wy - eyePos.y;
            float mz = wz - eyePos.z;

            float light = computeEntityLight(world, wx, wy + 0.8f, wz);
            float r = light, g = light, b = light;
            if (m.hurtTime > 0 || m.deathTime > 0) { r = 1.0f; g = 0.30f; b = 0.30f; }
            if (m.type == MOB_CREEPER && m.creeperFuse > 0 && ((m.creeperFuse / 3) & 1)) {
                r = 1.4f; g = 1.4f; b = 1.4f;
            }

            appendMobGeometry(m, mx, my, mz, alpha, r, g, b);

            float shadowRad = (m.type == MOB_SPIDER) ? 0.75f : ((m.type == MOB_GHAST) ? 1.8f : 0.45f);
            appendGroundShadow(world, wx, wy, wz, eyePos, shadowRad, 0.55f);
        }

        // 2. Batch Dropped Items + Small Shadow
        for (const DroppedItemEntity& d : entities.droppedItems) {
            float wx = d.prevX + (d.x - d.prevX) * alpha;
            float wy = d.prevY + (d.y - d.prevY) * alpha;
            float wz = d.prevZ + (d.z - d.prevZ) * alpha;

            float ix = wx - eyePos.x;
            float iy = wy - eyePos.y + std::sin(totalTimeSec * 3.0f + d.bobPhase) * 0.08f;
            float iz = wz - eyePos.z;
            float light = computeEntityLight(world, wx, wy, wz);
            float spinDeg = (totalTimeSec * 90.0f) + d.bobPhase * 57.29578f;

            if (d.stack.id < 256) {
                uint16_t rawSt = (uint16_t)((d.stack.id << 4) | (d.stack.damage & 0x0F));
                uint16_t spr = world.registry.resolveFaceTexture(rawSt, FACE_NORTH);
                appendMiniCube(terrainBatch, ix, iy, iz, 0.25f, spinDeg, spr, 16.0f, light, light, light);
            } else {
                uint16_t spr = assets.getItemDef(d.stack.id).spriteIdx;
                appendBillboardSprite(itemBatch, ix, iy, iz, 0.32f, spinDeg, spr, 16.0f, light, light, light);
            }
            appendGroundShadow(world, wx, wy, wz, eyePos, 0.18f, 0.40f);
        }

        // 3. Batch Primed TNT & Falling Sand/Gravel
        for (const PrimedTntEntity& tnt : redstone.primedTnts) {
            float tx = tnt.x - eyePos.x, ty = tnt.y - eyePos.y, tz = tnt.z - eyePos.z;
            bool flashWhite = ((tnt.fuseTicks / 5) & 1) != 0;
            float c = flashWhite ? 1.5f : 1.0f;
            appendMiniCube(terrainBatch, tx, ty + 0.5f, tz, 1.0f, 0.0f, SPR_TNT_SIDE, 16.0f, c, c, c);
            appendGroundShadow(world, tnt.x, tnt.y, tnt.z, eyePos, 0.5f, 0.5f);
        }
        for (const FallingBlockEntity& fb : world.fallingBlocks) {
            float fx = fb.x - eyePos.x, fy = fb.y - eyePos.y, fz = fb.z - eyePos.z;
            uint16_t spr = world.registry.resolveFaceTexture(fb.state, FACE_NORTH);
            float light = computeEntityLight(world, fb.x, fb.y, fb.z);
            appendMiniCube(terrainBatch, fx, fy + 0.5f, fz, 1.0f, 0.0f, spr, 16.0f, light, light, light);
        }

        // 4. Batch Projectiles & Block Particles
        for (const ProjectileEntity& pr : combat.projectiles) {
            float px = pr.x - eyePos.x, py = pr.y - eyePos.y, pz = pr.z - eyePos.z;
            float light = computeEntityLight(world, pr.x, pr.y, pr.z);
            uint16_t spr = (pr.kind == PROJ_ARROW) ? 37 : 14;
            appendBillboardSprite(itemBatch, px, py, pz, 0.35f, pr.yaw, spr, 16.0f, light, light, light);
        }
        for (const BlockParticle& pt : entities.particles) {
            float px = pt.x - eyePos.x, py = pt.y - eyePos.y, pz = pt.z - eyePos.z;
            appendMiniCube(terrainBatch, px, py, pz, pt.size, 0.0f, pt.spriteIdx, 16.0f, 0.85f, 0.85f, 0.85f);
        }

        // 5. Execute Batched OpenGL Draw Calls
        glUseProgram(shader);
        glUniformMatrix4fv(glGetUniformLocation(shader, "u_viewProj"), 1, GL_FALSE, viewProj.m);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        glActiveTexture(GL_TEXTURE0);
        for (int s = 0; s < SKIN_COUNT; ++s) {
            if (!skinBatches[s].empty() && assets.mobSkins[s]) {
                glBindTexture(GL_TEXTURE_2D, assets.mobSkins[s]);
                drawBatch(skinBatches[s]);
            }
        }

        glDisable(GL_CULL_FACE);
        if (!terrainBatch.empty() && assets.terrainTex) {
            glBindTexture(GL_TEXTURE_2D, assets.terrainTex);
            drawBatch(terrainBatch);
        }
        if (!itemBatch.empty() && assets.itemsTex) {
            glBindTexture(GL_TEXTURE_2D, assets.itemsTex);
            drawBatch(itemBatch);
        }

        // Render Grounded Entity Shadows (misc/shadow.png)
        if (!shadowBatch.empty() && assets.shadowTex) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glBindTexture(GL_TEXTURE_2D, assets.shadowTex);
            drawBatch(shadowBatch);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
        glEnable(GL_CULL_FACE);
    }

private:
    static float computeEntityLight(const WorldMap& world, float wx, float wy, float wz) {
        int bx = (int)std::floor(wx), by = (int)std::floor(wy), bz = (int)std::floor(wz);
        float sky = (world.getSkyLightAt(bx, by, bz) / 15.0f) * world.getSkyBrightness();
        float blk = world.getBlockLightAt(bx, by, bz) / 15.0f;
        return std::clamp(std::max(sky, blk), 0.12f, 1.0f);
    }

    // Mojang Render.renderShadow: projects circular shadow onto solid blocks beneath entity
    void appendGroundShadow(const WorldMap& world, float wx, float wy, float wz,
                            const Vec3& eyePos, float radius, float strength) {
        int bx0 = (int)std::floor(wx - radius);
        int bx1 = (int)std::floor(wx + radius);
        int bz0 = (int)std::floor(wz - radius);
        int bz1 = (int)std::floor(wz + radius);
        int by  = (int)std::floor(wy);

        for (int bz = bz0; bz <= bz1; ++bz) {
            for (int bx = bx0; bx <= bx1; ++bx) {
                for (int dy = 0; dy <= 2; ++dy) {
                    int checkY = by - 1 - dy;
                    if (checkY < 0) break;
                    uint16_t st = world.getBlockAt(bx, checkY, bz);
                    if (world.registry.get(st).isSolid && !world.registry.get(world.getBlockAt(bx, checkY + 1, bz)).isSolid) {
                        float distY = wy - (checkY + 1.0f);
                        float alpha = strength * (1.0f - distY * 0.35f);
                        if (alpha <= 0.02f) break;

                        float x0 = bx - eyePos.x, x1 = (bx + 1.0f) - eyePos.x;
                        float z0 = bz - eyePos.z, z1 = (bz + 1.0f) - eyePos.z;
                        float sy = (checkY + 1.005f) - eyePos.y;

                        float u0 = ((bx - wx) / (radius * 2.0f)) + 0.5f;
                        float u1 = ((bx + 1.0f - wx) / (radius * 2.0f)) + 0.5f;
                        float v0 = ((bz - wz) / (radius * 2.0f)) + 0.5f;
                        float v1 = ((bz + 1.0f - wz) / (radius * 2.0f)) + 0.5f;

                        EntityVertex q[4] = {
                            { x0, sy, z1, 0, 1, 0, u0, v1, 1, 1, 1, alpha },
                            { x1, sy, z1, 0, 1, 0, u1, v1, 1, 1, 1, alpha },
                            { x1, sy, z0, 0, 1, 0, u1, v0, 1, 1, 1, alpha },
                            { x0, sy, z0, 0, 1, 0, u0, v0, 1, 1, 1, alpha }
                        };
                        shadowBatch.push_back(q[0]); shadowBatch.push_back(q[1]); shadowBatch.push_back(q[2]);
                        shadowBatch.push_back(q[0]); shadowBatch.push_back(q[2]); shadowBatch.push_back(q[3]);
                        break;
                    }
                }
            }
        }
    }

    static void appendSkinBox(std::vector<EntityVertex>& dst,
                              float baseX, float baseY, float baseZ,
                              float bodyYawDeg, float deathRollDeg,
                              float pivX, float pivY, float pivZ,
                              float rotXDeg, float rotYDeg, float rotZDeg,
                              float offX, float offY, float offZ,
                              float wPx, float hPx, float dPx,
                              int texU, int texV, float r, float g, float b, float inflatePx = 0.0f) {
        const float pxScale = 1.0f / 16.0f;
        float x0 = (offX - inflatePx) * pxScale, x1 = (offX + wPx + inflatePx) * pxScale;
        float y0 = (offY - inflatePx) * pxScale, y1 = (offY + hPx + inflatePx) * pxScale;
        float z0 = (offZ - inflatePx) * pxScale, z1 = (offZ + dPx + inflatePx) * pxScale;

        Mat4 xf = Mat4::translate(baseX, baseY, baseZ) *
                  Mat4::rotateY(-bodyYawDeg) *
                  Mat4::rotateZ(deathRollDeg) *
                  Mat4::translate(pivX * pxScale, pivY * pxScale, pivZ * pxScale) *
                  Mat4::rotateZ(rotZDeg) *
                  Mat4::rotateY(rotYDeg) *
                  Mat4::rotateX(rotXDeg);

        auto xformPt = [&](float lx, float ly, float lz) -> Vec3 {
            return {
                xf.m[0] * lx + xf.m[4] * ly + xf.m[8]  * lz + xf.m[12],
                xf.m[1] * lx + xf.m[5] * ly + xf.m[9]  * lz + xf.m[13],
                xf.m[2] * lx + xf.m[6] * ly + xf.m[10] * lz + xf.m[14]
            };
        };

        auto xformNorm = [&](float nx, float ny, float nz) -> Vec3 {
            return Vec3{
                xf.m[0] * nx + xf.m[4] * ny + xf.m[8]  * nz,
                xf.m[1] * nx + xf.m[5] * ny + xf.m[9]  * nz,
                xf.m[2] * nx + xf.m[6] * ny + xf.m[10] * nz
            }.normalized();
        };

        const float invW = 1.0f / 64.0f;
        const float invH = 1.0f / 32.0f;

        auto emitSkinFace = [&](const float pts[4][3], float nx, float ny, float nz, int u0, int v0, int u1, int v1) {
            Vec3 p0 = xformPt(pts[0][0], pts[0][1], pts[0][2]);
            Vec3 p1 = xformPt(pts[1][0], pts[1][1], pts[1][2]);
            Vec3 p2 = xformPt(pts[2][0], pts[2][1], pts[2][2]);
            Vec3 p3 = xformPt(pts[3][0], pts[3][1], pts[3][2]);
            Vec3 n  = xformNorm(nx, ny, nz);

            float fu0 = u0 * invW, fv0 = v0 * invH, fu1 = u1 * invW, fv1 = v1 * invH;
            EntityVertex v[4] = {
                { p0.x, p0.y, p0.z, n.x, n.y, n.z, fu0, fv1, r, g, b, 1.0f },
                { p1.x, p1.y, p1.z, n.x, n.y, n.z, fu1, fv1, r, g, b, 1.0f },
                { p2.x, p2.y, p2.z, n.x, n.y, n.z, fu1, fv0, r, g, b, 1.0f },
                { p3.x, p3.y, p3.z, n.x, n.y, n.z, fu0, fv0, r, g, b, 1.0f }
            };
            dst.push_back(v[0]); dst.push_back(v[1]); dst.push_back(v[2]);
            dst.push_back(v[0]); dst.push_back(v[2]); dst.push_back(v[3]);
        };

        int iu = texU, iv = texV, iw = (int)wPx, ih = (int)hPx, id = (int)dPx;

        float fS[4][3] = {{x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}};
        emitSkinFace(fS, 0, 0, 1, iu + id, iv + id, iu + id + iw, iv + id + ih);

        float fN[4][3] = {{x1,y0,z0}, {x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}};
        emitSkinFace(fN, 0, 0, -1, iu + id + iw + id, iv + id, iu + id + iw + id + iw, iv + id + ih);

        float fE[4][3] = {{x1,y0,z1}, {x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}};
        emitSkinFace(fE, 1, 0, 0, iu + id + iw, iv + id, iu + id + iw + id, iv + id + ih);

        float fW[4][3] = {{x0,y0,z0}, {x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}};
        emitSkinFace(fW, -1, 0, 0, iu, iv + id, iu + id, iv + id + ih);

        float fT[4][3] = {{x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, {x0,y1,z0}};
        emitSkinFace(fT, 0, 1, 0, iu + id, iv, iu + id + iw, iv + id);

        float fB[4][3] = {{x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, {x0,y0,z1}};
        emitSkinFace(fB, 0, -1, 0, iu + id + iw, iv, iu + id + iw + iw, iv + id);
    }

    void appendMobGeometry(const MobEntity& m, float x, float y, float z, float alpha, float r, float g, float b) {
        float bodyYaw = m.prevBodyYaw + BetaEntityManager::wrapDegrees(m.bodyYaw - m.prevBodyYaw) * alpha;
        float headYaw = m.prevHeadYaw + BetaEntityManager::wrapDegrees(m.headYaw - m.prevHeadYaw) * alpha;
        float headRelYaw = BetaEntityManager::wrapDegrees(headYaw - bodyYaw);
        float headPitch  = m.prevPitch + (m.pitch - m.prevPitch) * alpha;

        float deathProg = (m.deathTime > 0) ? std::min(1.0f, (m.deathTime + alpha) / 20.0f) : 0.0f;
        float deathRoll = std::sqrt(deathProg) * 90.0f;

        float swing   = std::cos(m.limbSwing * 0.6662f) * 65.0f * m.limbSwingAmt;
        float breathe = std::sin((m.ageTicks + alpha) * 0.067f) * 3.0f;

        if (m.type == MOB_ZOMBIE || m.type == MOB_SKELETON) {
            MobSkinId sk = (m.type == MOB_ZOMBIE) ? SKIN_ZOMBIE : SKIN_SKELETON;
            auto& dst = skinBatches[sk];
            float limbW   = (m.type == MOB_SKELETON) ? 2.0f : 4.0f;
            float limbOff = (m.type == MOB_SKELETON) ? -1.0f : -2.0f;

            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 24, 0, headPitch, -headRelYaw, 0, -4, 0, -4, 8, 8, 8, 0, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 24, 0, 0, 0, 0, -4, -12, -2, 8, 12, 4, 16, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -2, 12, 0,  swing, 0, 0, limbOff, -12, limbOff, limbW, 12, limbW, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  2, 12, 0, -swing, 0, 0, limbOff, -12, limbOff, limbW, 12, limbW, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -6, 22, 0, -90.0f + breathe, 0, -breathe, limbOff, -10, limbOff, limbW, 12, limbW, 40, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  6, 22, 0, -90.0f - breathe, 0,  breathe, limbOff, -10, limbOff, limbW, 12, limbW, 40, 16, r, g, b);
        } else if (m.type == MOB_CREEPER) {
            auto& dst = skinBatches[SKIN_CREEPER];
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 18, 0, headPitch, -headRelYaw, 0, -4, 0, -4, 8, 8, 8, 0, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 18, 0, 0, 0, 0, -4, -12, -2, 8, 12, 4, 16, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -2, 6,  4,  swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  2, 6,  4, -swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -2, 6, -4, -swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  2, 6, -4,  swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b);
        } else if (m.type == MOB_PIG) {
            auto& dst = skinBatches[SKIN_PIG];
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 12, 6, headPitch, -headRelYaw, 0, -4, -4, -4, 8, 8, 8, 0, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 10, 0, 0, 0, 0, -5, -4, -8, 10, 8, 16, 28, 8, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -3, 6,  5,  swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  3, 6,  5, -swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -3, 6, -5, -swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  3, 6, -5,  swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b);
        } else if (m.type == MOB_SHEEP) {
            auto& dst = skinBatches[SKIN_SHEEP];
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 18, 6, headPitch, -headRelYaw, 0, -3, -3, -4, 6, 6, 8, 0, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 15, 0, 0, 0, 0, -4, -3, -8, 8, 6, 16, 28, 8, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -3, 12,  5,  swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  3, 12,  5, -swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -3, 12, -5, -swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  3, 12, -5,  swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);

            if (!m.sheared) {
                static constexpr float woolRGB[16][3] = {
                    {1.0f,1.0f,1.0f},{0.85f,0.5f,0.2f},{0.7f,0.3f,0.85f},{0.4f,0.6f,0.85f},
                    {0.9f,0.9f,0.2f},{0.5f,0.8f,0.1f},{0.95f,0.5f,0.65f},{0.3f,0.3f,0.3f},
                    {0.6f,0.6f,0.6f},{0.3f,0.5f,0.6f},{0.5f,0.25f,0.7f},{0.2f,0.3f,0.7f},
                    {0.4f,0.3f,0.2f},{0.4f,0.5f,0.2f},{0.6f,0.2f,0.2f},{0.1f,0.1f,0.1f}
                };
                const float* wc = woolRGB[m.woolColor & 15];
                auto& fur = skinBatches[SKIN_SHEEP_FUR];
                appendSkinBox(fur, x, y, z, bodyYaw, deathRoll, 0, 18, 6, headPitch, -headRelYaw, 0, -3, -3, -4, 6, 6, 6, 0, 0, r * wc[0], g * wc[1], b * wc[2], 0.6f);
                appendSkinBox(fur, x, y, z, bodyYaw, deathRoll, 0, 15, 0, 0, 0, 0, -4, -3, -8, 8, 6, 16, 28, 8, r * wc[0], g * wc[1], b * wc[2], 1.5f);
            }
        } else if (m.type == MOB_COW) {
            auto& dst = skinBatches[SKIN_COW];
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 20, 8, headPitch, -headRelYaw, 0, -4, -4, -3, 8, 8, 6, 0, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 17, 0, 0, 0, 0, -6, -5, -9, 12, 10, 18, 18, 4, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -4, 12,  6,  swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  4, 12,  6, -swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -4, 12, -6, -swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  4, 12, -6,  swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
        } else if (m.type == MOB_SPIDER) {
            auto& dst = skinBatches[SKIN_SPIDER];
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 9,  7, headPitch, -headRelYaw, 0, -4, -4, -4, 8, 8, 8, 32, 4, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 9,  0, 0, 0, 0, -3, -3, -3, 6, 6, 6, 0, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 9, -8, 0, 0, 0, -5, -4, -6, 10, 8, 12, 0, 12, r, g, b);
            for (int side = -1; side <= 1; side += 2) {
                for (int leg = 0; leg < 4; ++leg) {
                    float zOff = (leg - 1.5f) * 2.2f;
                    float legSwing = ((leg & 1) ? swing : -swing) * 0.45f;
                    appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, side * 4.0f, 9, zOff, 0, legSwing, side * 35.0f,
                                  (side < 0) ? -14.0f : 0.0f, -1.0f, -1.0f, 14, 2, 2, 18, 0, r, g, b);
                }
            }
        }
    }

    static void appendMiniCube(std::vector<EntityVertex>& dst, float cx, float cy, float cz,
                               float size, float yawDeg, uint16_t spriteIdx, float gridDim,
                               float r, float g, float b) {
        float h = size * 0.5f;
        float inv = 1.0f / gridDim;
        float u0 = (spriteIdx % (int)gridDim) * inv + 0.001f;
        float v0 = (spriteIdx / (int)gridDim) * inv + 0.001f;
        float u1 = u0 + inv - 0.002f, v1 = v0 + inv - 0.002f;

        Mat4 rot = Mat4::rotateY(yawDeg);
        auto emitFace = [&](float p[4][3], float nx, float ny, float nz) {
            EntityVertex v[4];
            float uvs[4][2] = {{u0,v1},{u1,v1},{u1,v0},{u0,v0}};
            for (int i = 0; i < 4; ++i) {
                float rx = rot.m[0] * p[i][0] + rot.m[8] * p[i][2] + cx;
                float ry = p[i][1] + cy;
                float rz = rot.m[2] * p[i][0] + rot.m[10] * p[i][2] + cz;
                v[i] = { rx, ry, rz, nx, ny, nz, uvs[i][0], uvs[i][1], r, g, b, 1.0f };
            }
            dst.push_back(v[0]); dst.push_back(v[1]); dst.push_back(v[2]);
            dst.push_back(v[0]); dst.push_back(v[2]); dst.push_back(v[3]);
        };

        float fS[4][3]={{-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}};     emitFace(fS, 0,0,1);
        float fN[4][3]={{h,-h,-h},{-h,-h,-h},{-h,h,-h},{h,h,-h}}; emitFace(fN, 0,0,-1);
        float fE[4][3]={{h,-h,h},{h,-h,-h},{h,h,-h},{h,h,h}};     emitFace(fE, 1,0,0);
        float fW[4][3]={{-h,-h,-h},{-h,-h,h},{-h,h,h},{-h,h,-h}}; emitFace(fW,-1,0,0);
        float fT[4][3]={{-h,h,h},{h,h,h},{h,h,-h},{-h,h,-h}};     emitFace(fT, 0,1,0);
        float fB[4][3]={{-h,-h,-h},{h,-h,-h},{h,-h,h},{-h,-h,h}}; emitFace(fB, 0,-1,0);
    }

    static void appendBillboardSprite(std::vector<EntityVertex>& dst, float cx, float cy, float cz,
                                      float size, float yawDeg, uint16_t spriteIdx, float gridDim,
                                      float r, float g, float b) {
        float h = size * 0.5f;
        float inv = 1.0f / gridDim;
        float u0 = (spriteIdx % (int)gridDim) * inv + 0.001f;
        float v0 = (spriteIdx / (int)gridDim) * inv + 0.001f;
        float u1 = u0 + inv - 0.002f, v1 = v0 + inv - 0.002f;

        float rad = yawDeg * 0.0174532925f;
        float rx = std::cos(rad) * h, rz = std::sin(rad) * h;

        EntityVertex v[4] = {
            { cx - rx, cy - h, cz - rz, 0, 1, 0, u0, v1, r, g, b, 1.0f },
            { cx + rx, cy - h, cz + rz, 0, 1, 0, u1, v1, r, g, b, 1.0f },
            { cx + rx, cy + h, cz + rz, 0, 1, 0, u1, v0, r, g, b, 1.0f },
            { cx - rx, cy + h, cz - rz, 0, 1, 0, u0, v0, r, g, b, 1.0f }
        };
        dst.push_back(v[0]); dst.push_back(v[1]); dst.push_back(v[2]);
        dst.push_back(v[0]); dst.push_back(v[2]); dst.push_back(v[3]);
    }

    void drawBatch(const std::vector<EntityVertex>& verts) {
        if (verts.empty()) return;
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(EntityVertex), verts.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(EntityVertex), (void*)offsetof(EntityVertex, x));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(EntityVertex), (void*)offsetof(EntityVertex, nx));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(EntityVertex), (void*)offsetof(EntityVertex, u));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(EntityVertex), (void*)offsetof(EntityVertex, r));
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    }
};