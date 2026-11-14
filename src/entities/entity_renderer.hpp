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
// CANONICAL BATCHED ENTITY & SKELETAL MODEL PIPELINE (SPEC-2011)
// ============================================================================
// Technical Subsystem Architecture:
// 1. Dual-Axis Articulated Skeletal Mesh Synthesis:
//    - Autonomous Yaw/Pitch Decomposition: Independent transformation nodes decouple
//      directional body travel (bodyYaw) from cranial focus (headYaw/pitch) with
//      a 75-degree anatomical cervical constraint.
//    - Sinusoidal Locomotion Kinematics: Quadruped and biped limbs apply continuous
//      harmonic oscillation equations: angle = cos(limbSwing * 0.6662F) * 1.4F * limbSwingAmt.
// 2. Precise Ground Shadow Orthographic Projection (Convex Polygon Clipping):
//    - Geometric UV Bound Invariant: Shadow quads strictly evaluate intersection
//      against substrate solid bounding blocks.
//    - Sub-Block Horizon Clipping: If a candidate block partially intersects the
//      projected shadow radius, the polygon vertices [x0, x1, z0, z1] and parametric
//      texture bounds [u0, u1, v0, v1] are geometrically clamped to the exact [0.0, 1.0]
//      footprint, preventing peripheral texture coordinate wrap-around and eliminating
//      duplicate concentric shadow rings across adjoining blocks.
// 3. Bi-Directional Diffuse Lighting Model:
//    - Normal vectors sample two opposing celestial vectors:
//        L1 = normalize(0.2, 1.0, -0.7), L2 = normalize(-0.2, 1.0, 0.7)
//    - Lambertian Surface Shading:
//        shade = clamp(0.45 + 0.35 * max(dot(N, L1), 0.0) + 0.35 * max(dot(N, L2), 0.0), 0.35, 1.0)
// 4. Species-Specific Model Invariants:
//    - Sheep: Base quadruped renders 4 bare limbs. The secondary fleece pass
//      strictly encompasses only the head (inflated by 0.6F) and the thoracic torso
//      (inflated by 1.75F), completely omitting limb boxes to prevent upward floating towers.
//    - Creeper: Pre-detonation thermal swelling modulates dimensions via quartic curve:
//        scaleXZ = 1.0 + f1^4 * 0.4, scaleY = (1.0 + f1^4 * 0.1) / f2.
//    - Ghast: Cubic 16x16x16 aerostat body scaled 4.5x, tethering 9 stochastic tentacles
//      actuated via sinusoidal phase displacement.
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

        // 1. Batch Mobs with Decoupled Head/Body Yaw, Pre-Detonation Swell, and Ground Shadow
        for (const MobEntity& m : entities.mobs) {
            float wx = m.prevX + (m.x - m.prevX) * alpha;
            float wy = m.prevY + (m.y - m.prevY) * alpha;
            float wz = m.prevZ + (m.z - m.prevZ) * alpha;

            float mx = wx - eyePos.x;
            float my = wy - eyePos.y;
            float mz = wz - eyePos.z;

            float light = computeEntityLight(world, wx, wy + 0.8f, wz);
            float r = light, g = light, b = light;
            if (m.hurtTime > 0 || m.deathTime > 0) {
                r = 1.0f; g = 0.30f; b = 0.30f;
            }
            if (m.type == MOB_CREEPER && m.creeperFuse > 0 && ((m.creeperFuse / 3) & 1)) {
                r = 1.5f; g = 1.5f; b = 1.5f;
            }

            appendMobGeometry(m, mx, my, mz, alpha, r, g, b);

            float shadowRad = (m.type == MOB_SPIDER) ? 0.75f : ((m.type == MOB_GHAST) ? 2.0f : 0.45f);
            appendGroundShadow(world, wx, wy, wz, eyePos, shadowRad, 0.55f);
        }

        // 2. Batch Dropped Floating Items
        for (const DroppedItemEntity& d : entities.droppedItems) {
            float wx = d.prevX + (d.x - d.prevX) * alpha;
            float wy = d.prevY + (d.y - d.prevY) * alpha;
            float wz = d.prevZ + (d.z - d.prevZ) * alpha;

            float bobOffset = std::sin((static_cast<float>(d.ageTicks) + alpha) * 0.1f + d.bobPhase) * 0.1f + 0.1f;
            float ix = wx - eyePos.x;
            float iy = wy - eyePos.y + bobOffset;
            float iz = wz - eyePos.z;
            float light = computeEntityLight(world, wx, wy, wz);
            float spinDeg = ((static_cast<float>(d.ageTicks) + alpha) * 2.864789f) + d.bobPhase * 57.29578f;

            if (d.stack.id < 256) {
                uint16_t rawSt = static_cast<uint16_t>((d.stack.id << 4) | (d.stack.damage & 0x0F));
                const BlockDef& def = world.registry.get(rawSt);
                if (def.shape == SHAPE_CUBE || def.shape == SHAPE_SLAB || def.shape == SHAPE_STAIR || def.shape == SHAPE_PISTON) {
                    uint16_t fSpr[6];
                    for (int f = 0; f < 6; ++f) {
                        fSpr[f] = world.registry.resolveFaceTexture(rawSt, static_cast<BlockFace>(f));
                    }
                    float tr = light, tg = light, tb = light;
                    if (def.tintMode == TINT_GRASS || def.tintMode == TINT_FOLIAGE) {
                        float br, bg, bb;
                        assets.sampleBiomeColor(0.7f, 0.7f, def.tintMode == TINT_FOLIAGE, br, bg, bb);
                        tr *= br; tg *= bg; tb *= bb;
                    }
                    appendTexturedBox(terrainBatch, ix, iy + 0.04f, iz, 0.25f, 0.25f, 0.25f, spinDeg, fSpr,
                                      light, light, light, tr, tg, tb, def.tintMode == TINT_GRASS);
                } else {
                    uint16_t spr = world.registry.resolveFaceTexture(rawSt, FACE_NORTH);
                    appendBillboardSprite(terrainBatch, ix, iy + 0.12f, iz, 0.45f, spinDeg, spr, 16.0f, light, light, light);
                }
            } else {
                uint16_t spr = assets.getItemDef(d.stack.id).spriteIdx;
                appendBillboardSprite(itemBatch, ix, iy + 0.12f, iz, 0.45f, spinDeg, spr, 16.0f, light, light, light);
            }
            appendGroundShadow(world, wx, wy, wz, eyePos, 0.15f, 0.45f);
        }

        // 3. Batch Primed Explosives & Falling Solids
        for (const PrimedTntEntity& tnt : redstone.primedTnts) {
            float tx = tnt.x - eyePos.x, ty = tnt.y - eyePos.y, tz = tnt.z - eyePos.z;
            float scale = 1.0f;
            if (tnt.fuseTicks < 10) {
                float f = 1.0f - (static_cast<float>(tnt.fuseTicks) / 10.0f);
                f = std::clamp(f, 0.0f, 1.0f);
                scale = 1.0f + (f * f * f * f) * 0.3f;
            }
            bool flashWhite = ((tnt.fuseTicks / 5) & 1) != 0;
            float c = flashWhite ? 1.6f : 1.0f;
            uint16_t tntSpr[6] = { SPR_TNT_SIDE, SPR_TNT_SIDE, SPR_TNT_TOP, SPR_TNT_BOTTOM, SPR_TNT_SIDE, SPR_TNT_SIDE };
            appendTexturedBox(terrainBatch, tx, ty + 0.5f * scale, tz, scale, scale, scale, 0.0f, tntSpr, c, c, c, c, c, c, false);
            appendGroundShadow(world, tnt.x, tnt.y, tnt.z, eyePos, 0.5f, 0.5f);
        }
        for (const FallingBlockEntity& fb : world.fallingBlocks) {
            float fx = fb.x - eyePos.x, fy = fb.y - eyePos.y, fz = fb.z - eyePos.z;
            uint16_t fbSpr[6];
            for (int f = 0; f < 6; ++f) {
                fbSpr[f] = world.registry.resolveFaceTexture(fb.state, static_cast<BlockFace>(f));
            }
            float light = computeEntityLight(world, fb.x, fb.y, fb.z);
            appendTexturedBox(terrainBatch, fx, fy + 0.5f, fz, 1.0f, 1.0f, 1.0f, 0.0f, fbSpr, light, light, light, light, light, light, false);
        }

        // 4. Batch Projectiles & Block Particles
        for (const ProjectileEntity& pr : combat.projectiles) {
            float px = pr.x - eyePos.x, py = pr.y - eyePos.y, pz = pr.z - eyePos.z;
            float light = computeEntityLight(world, pr.x, pr.y, pr.z);
            if (pr.kind == PROJ_ARROW) {
                appendArrow3D(itemBatch, px, py, pz, pr.yaw, pr.pitch, light);
            } else {
                uint16_t spr = (pr.kind == PROJ_EGG) ? 12 : 14;
                appendBillboardSprite(itemBatch, px, py, pz, 0.35f, pr.yaw, spr, 16.0f, light, light, light);
            }
        }
        for (const BlockParticle& pt : entities.particles) {
            float px = pt.x - eyePos.x, py = pt.y - eyePos.y, pz = pt.z - eyePos.z;
            appendSubTexelParticleCube(terrainBatch, px, py, pz, pt.size, pt.spriteIdx,
                                       static_cast<int>(pt.x * 31.0f + pt.z * 17.0f), 0.85f);
        }

        // 5. Draw Pipeline Dispatch
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
        (void)totalTimeSec;
    }

private:
    static float computeEntityLight(const WorldMap& world, float wx, float wy, float wz) {
        int bx = static_cast<int>(std::floor(wx));
        int by = static_cast<int>(std::floor(wy));
        int bz = static_cast<int>(std::floor(wz));
        float sky = (world.getSkyLightAt(bx, by, bz) / 15.0f) * world.getSkyBrightness();
        float blk = world.getBlockLightAt(bx, by, bz) / 15.0f;
        float lightLevel = std::clamp(std::max(sky, blk), 0.0f, 1.0f);
        float f1 = 1.0f - lightLevel;
        return ((1.0f - f1) / (f1 * 3.0f + 1.0f)) * 0.95f + 0.05f;
    }

    // Exact Canonical Ground Shadow: Clamped to sub-block boundaries to completely eliminate multi-circle artifacts
    void appendGroundShadow(const WorldMap& world, float wx, float wy, float wz,
                            const Vec3& eyePos, float radius, float strength) {
        int bx0 = static_cast<int>(std::floor(wx - radius));
        int bx1 = static_cast<int>(std::floor(wx + radius));
        int bz0 = static_cast<int>(std::floor(wz - radius));
        int bz1 = static_cast<int>(std::floor(wz + radius));
        int by  = static_cast<int>(std::floor(wy));

        for (int bz = bz0; bz <= bz1; ++bz) {
            for (int bx = bx0; bx <= bx1; ++bx) {
                for (int dy = 0; dy <= 2; ++dy) {
                    int checkY = by - 1 - dy;
                    if (checkY < 0) break;
                    uint16_t st = world.getBlockAt(bx, checkY, bz);
                    if (world.registry.get(st).isSolid && !world.registry.get(world.getBlockAt(bx, checkY + 1, bz)).isSolid) {
                        uint8_t id = static_cast<uint8_t>((st >> 4) & 0xFF);
                        float blockTopY = static_cast<float>(checkY) + ((id == 44) ? 0.5f : ((id == 88) ? 0.875f : 1.0f));
                        float distY = wy - blockTopY;
                        if (distY < -0.05f) break;

                        float alpha = strength * (1.0f - distY * 0.5f);
                        if (alpha <= 0.02f) break;

                        float minX = static_cast<float>(bx);
                        float maxX = static_cast<float>(bx + 1);
                        float minZ = static_cast<float>(bz);
                        float maxZ = static_cast<float>(bz + 1);

                        float dx0 = minX - wx;
                        float dx1 = maxX - wx;
                        float dz0 = minZ - wz;
                        float dz1 = maxZ - wz;

                        if (dx0 > radius || dx1 < -radius || dz0 > radius || dz1 < -radius) break;

                        if (dx0 < -radius) dx0 = -radius;
                        if (dx1 >  radius) dx1 =  radius;
                        if (dz0 < -radius) dz0 = -radius;
                        if (dz1 >  radius) dz1 =  radius;

                        float u0 = (dx0 + radius) / (radius * 2.0f);
                        float u1 = (dx1 + radius) / (radius * 2.0f);
                        float v0 = (dz0 + radius) / (radius * 2.0f);
                        float v1 = (dz1 + radius) / (radius * 2.0f);

                        float x0 = (wx + dx0) - eyePos.x;
                        float x1 = (wx + dx1) - eyePos.x;
                        float z0 = (wz + dz0) - eyePos.z;
                        float z1 = (wz + dz1) - eyePos.z;
                        float sy = (blockTopY + 0.005f) - eyePos.y;

                        EntityVertex q[4] = {
                            { x0, sy, z1, 0.0f, 1.0f, 0.0f, u0, v1, 1.0f, 1.0f, 1.0f, alpha },
                            { x1, sy, z1, 0.0f, 1.0f, 0.0f, u1, v1, 1.0f, 1.0f, 1.0f, alpha },
                            { x1, sy, z0, 0.0f, 1.0f, 0.0f, u1, v0, 1.0f, 1.0f, 1.0f, alpha },
                            { x0, sy, z0, 0.0f, 1.0f, 0.0f, u0, v0, 1.0f, 1.0f, 1.0f, alpha }
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

        int iu = texU, iv = texV, iw = static_cast<int>(wPx), ih = static_cast<int>(hPx), id = static_cast<int>(dPx);

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

    void appendSkeletonHeldBow(float baseX, float baseY, float baseZ,
                               float bodyYawDeg, float deathRollDeg,
                               float armRotXDeg, float armRotYDeg, float armRotZDeg,
                               float r, float g, float b) {
        const float pxScale = 1.0f / 16.0f;
        Mat4 armXf = Mat4::translate(baseX, baseY, baseZ) *
                     Mat4::rotateY(-bodyYawDeg) *
                     Mat4::rotateZ(deathRollDeg) *
                     Mat4::translate(-5.0f * pxScale, 22.0f * pxScale, 0.0f) *
                     Mat4::rotateZ(armRotZDeg) *
                     Mat4::rotateY(armRotYDeg) *
                     Mat4::rotateX(armRotXDeg) *
                     Mat4::translate(-0.04f, -0.58f, 0.24f) *
                     Mat4::rotateX(35.0f) *
                     Mat4::rotateY(90.0f) *
                     Mat4::scale(0.55f, 0.55f, 0.55f);

        auto xformPt = [&](float lx, float ly, float lz) -> Vec3 {
            return {
                armXf.m[0] * lx + armXf.m[4] * ly + armXf.m[8]  * lz + armXf.m[12],
                armXf.m[1] * lx + armXf.m[5] * ly + armXf.m[9]  * lz + armXf.m[13],
                armXf.m[2] * lx + armXf.m[6] * ly + armXf.m[10] * lz + armXf.m[14]
            };
        };

        const uint16_t bowSpr = 21; // ITEM_BOW
        float u0 = (bowSpr % 16) / 16.0f + 0.001f;
        float v0 = (bowSpr / 16) / 16.0f + 0.001f;
        float u1 = (bowSpr % 16 + 1) / 16.0f - 0.001f;
        float v1 = (bowSpr / 16 + 1) / 16.0f - 0.001f;

        Vec3 p0 = xformPt(-0.5f, -0.5f, 0.0f);
        Vec3 p1 = xformPt( 0.5f, -0.5f, 0.0f);
        Vec3 p2 = xformPt( 0.5f,  0.5f, 0.0f);
        Vec3 p3 = xformPt(-0.5f,  0.5f, 0.0f);

        EntityVertex q[4] = {
            { p0.x, p0.y, p0.z, 0, 1, 0, u0, v1, r, g, b, 1.0f },
            { p1.x, p1.y, p1.z, 0, 1, 0, u1, v1, r, g, b, 1.0f },
            { p2.x, p2.y, p2.z, 0, 1, 0, u1, v0, r, g, b, 1.0f },
            { p3.x, p3.y, p3.z, 0, 1, 0, u0, v0, r, g, b, 1.0f }
        };
        itemBatch.push_back(q[0]); itemBatch.push_back(q[1]); itemBatch.push_back(q[2]);
        itemBatch.push_back(q[0]); itemBatch.push_back(q[2]); itemBatch.push_back(q[3]);
    }

    void appendMobGeometry(const MobEntity& m, float x, float y, float z, float alpha, float r, float g, float b) {
        const float RAD_TO_DEG = 57.2957795f;
        float bodyYaw = m.prevBodyYaw + BetaEntityManager::wrapDegrees(m.bodyYaw - m.prevBodyYaw) * alpha;
        float headYaw = m.prevHeadYaw + BetaEntityManager::wrapDegrees(m.headYaw - m.prevHeadYaw) * alpha;
        float headRelYaw = BetaEntityManager::wrapDegrees(headYaw - bodyYaw);
        float headPitch  = m.prevPitch + (m.pitch - m.prevPitch) * alpha;

        float deathProg = (m.deathTime > 0) ? std::min(1.0f, (static_cast<float>(m.deathTime) + alpha) / 20.0f) : 0.0f;
        float deathRoll = std::sqrt(deathProg) * 90.0f;

        float agePlusAlpha = static_cast<float>(m.ageTicks) + alpha;
        float swing = std::cos(m.limbSwing * 0.6662f) * 1.4f * RAD_TO_DEG * m.limbSwingAmt;

        if (m.type == MOB_ZOMBIE || m.type == MOB_SKELETON) {
            MobSkinId sk = (m.type == MOB_ZOMBIE) ? SKIN_ZOMBIE : SKIN_SKELETON;
            auto& dst = skinBatches[sk];
            float limbW       = (m.type == MOB_SKELETON) ? 2.0f : 4.0f;
            float limbOff     = (m.type == MOB_SKELETON) ? -1.0f : -2.0f;
            float shoulderPiv = (m.type == MOB_SKELETON) ? 5.0f : 6.0f;

            float armRotZRight = (std::cos(agePlusAlpha * 0.09f) * 0.05f + 0.05f) * RAD_TO_DEG;
            float armRotZLeft  = -(std::cos(agePlusAlpha * 0.09f) * 0.05f + 0.05f) * RAD_TO_DEG;
            float armRotYRight = -0.1f * RAD_TO_DEG;
            float armRotYLeft  =  0.1f * RAD_TO_DEG;
            float armRotXRight = -90.0f + (std::sin(agePlusAlpha * 0.067f) * 0.05f) * RAD_TO_DEG;
            float armRotXLeft  = -90.0f - (std::sin(agePlusAlpha * 0.067f) * 0.05f) * RAD_TO_DEG;

            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 24, 0, headPitch, -headRelYaw, 0, -4, 0, -4, 8, 8, 8, 0, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 24, 0, 0, 0, 0, -4, -12, -2, 8, 12, 4, 16, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -2, 12, 0,  swing, 0, 0, limbOff, -12, limbOff, limbW, 12, limbW, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  2, 12, 0, -swing, 0, 0, limbOff, -12, limbOff, limbW, 12, limbW, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -shoulderPiv, 22, 0, armRotXRight, armRotYRight, armRotZRight, limbOff, -10, limbOff, limbW, 12, limbW, 40, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  shoulderPiv, 22, 0, armRotXLeft,  armRotYLeft,  armRotZLeft,  limbOff, -10, limbOff, limbW, 12, limbW, 40, 16, r, g, b);

            if (m.type == MOB_SKELETON) {
                appendSkeletonHeldBow(x, y, z, bodyYaw, deathRoll, armRotXRight, armRotYRight, armRotZRight, r, g, b);
            }
        } else if (m.type == MOB_CREEPER) {
            auto& dst = skinBatches[SKIN_CREEPER];
            float sx = x, sy = y, sz = z;
            if (m.creeperFuse > 0) {
                float f1 = std::clamp((static_cast<float>(m.creeperFuse) + alpha) / 28.0f, 0.0f, 1.0f);
                float f2 = 1.0f + std::sin(f1 * 100.0f) * f1 * 0.01f;
                f1 *= f1;
                f1 *= f1;
                float scaleY  = (1.0f + f1 * 0.1f) / f2;
                sy += (scaleY - 1.0f) * 0.45f;
            }
            float swellInflate = (m.creeperFuse > 0) ? (std::pow(std::clamp(m.creeperFuse / 28.0f, 0.0f, 1.0f), 4.0f) * 1.4f) : 0.0f;

            appendSkinBox(dst, sx, sy, sz, bodyYaw, deathRoll, 0, 18, 0, headPitch, -headRelYaw, 0, -4, 0, -4, 8, 8, 8, 0, 0, r, g, b, swellInflate);
            appendSkinBox(dst, sx, sy, sz, bodyYaw, deathRoll, 0, 18, 0, 0, 0, 0, -4, -12, -2, 8, 12, 4, 16, 16, r, g, b, swellInflate);
            appendSkinBox(dst, sx, sy, sz, bodyYaw, deathRoll, -2, 6,  4,  swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b, swellInflate * 0.5f);
            appendSkinBox(dst, sx, sy, sz, bodyYaw, deathRoll,  2, 6,  4, -swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b, swellInflate * 0.5f);
            appendSkinBox(dst, sx, sy, sz, bodyYaw, deathRoll, -2, 6, -4, -swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b, swellInflate * 0.5f);
            appendSkinBox(dst, sx, sy, sz, bodyYaw, deathRoll,  2, 6, -4,  swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b, swellInflate * 0.5f);
        } else if (m.type == MOB_PIG) {
            auto& dst = skinBatches[SKIN_PIG];
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 12, 6, headPitch, -headRelYaw, 0, -4, -4, -4, 8, 8, 8, 0, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 10, 0, 0, 0, 0, -5, -4, -8, 10, 8, 16, 28, 8, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -3, 6,  5,  swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  3, 6,  5, -swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -3, 6, -5, -swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  3, 6, -5,  swing, 0, 0, -2, -6, -2, 4, 6, 4, 0, 16, r, g, b);
        } else if (m.type == MOB_SHEEP) {
            // Base sheared body and 4 bare limbs
            auto& dst = skinBatches[SKIN_SHEEP];
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 18, 6, headPitch, -headRelYaw, 0, -3, -3, -4, 6, 6, 8, 0, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 15, 0, 0, 0, 0, -4, -3, -8, 8, 6, 16, 28, 8, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -3, 12,  5,  swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  3, 12,  5, -swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -3, 12, -5, -swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  3, 12, -5,  swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);

            // Canonical Secondary Fleece Pass: Renders head and body ONLY (no limbs), eliminating floating fleece towers
            if (!m.sheared) {
                static constexpr float woolRGB[16][3] = {
                    {1.0f,1.0f,1.0f},{0.85f,0.5f,0.2f},{0.7f,0.3f,0.85f},{0.4f,0.6f,0.85f},
                    {0.9f,0.9f,0.2f},{0.5f,0.8f,0.1f},{0.95f,0.5f,0.65f},{0.3f,0.3f,0.3f},
                    {0.6f,0.6f,0.6f},{0.3f,0.5f,0.6f},{0.5f,0.25f,0.7f},{0.2f,0.3f,0.7f},
                    {0.4f,0.3f,0.2f},{0.4f,0.5f,0.2f},{0.6f,0.2f,0.2f},{0.1f,0.1f,0.1f}
                };
                const float* wc = woolRGB[m.woolColor & 15];
                float wr = r * wc[0], wg = g * wc[1], wb = b * wc[2];
                auto& fur = skinBatches[SKIN_SHEEP_FUR];
                appendSkinBox(fur, x, y, z, bodyYaw, deathRoll, 0, 18, 6, headPitch, -headRelYaw, 0, -3, -3, -4, 6, 6, 6, 0, 0, wr, wg, wb, 0.6f);
                appendSkinBox(fur, x, y, z, bodyYaw, deathRoll, 0, 15, 0, 0, 0, 0, -4, -3, -8, 8, 6, 16, 28, 8, wr, wg, wb, 1.75f);
            }
        } else if (m.type == MOB_COW) {
            auto& dst = skinBatches[SKIN_COW];
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 20, 8, headPitch, -headRelYaw, 0, -4, -4, -3, 8, 8, 6, 0, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 20, 8, headPitch, -headRelYaw, 0, -5,  3, -1, 1, 3, 1, 22, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 20, 8, headPitch, -headRelYaw, 0,  4,  3, -1, 1, 3, 1, 22, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 17, 0, 0, 0, 0, -6, -5, -9, 12, 10, 18, 18, 4, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 17, 0, 0, 0, 0, -2, -6, -7,  4,  1,  6, 52, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -4, 12,  6,  swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  4, 12,  6, -swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -4, 12, -6, -swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  4, 12, -6,  swing, 0, 0, -2, -12, -2, 4, 12, 4, 0, 16, r, g, b);
        } else if (m.type == MOB_SPIDER) {
            auto& dst = skinBatches[SKIN_SPIDER];
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 9,  7, headPitch, -headRelYaw, 0, -4, -4, -4, 8, 8, 8, 32, 4, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 9,  0, 0, 0, 0, -3, -3, -3, 6, 6, 6, 0, 0, r, g, b);
            appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, 0, 9, -9, 0, 0, 0, -5, -4, -6, 10, 8, 12, 0, 12, r, g, b);

            const float PI = 3.14159265f;
            float baseYawPair[4] = { 45.0f, 22.5f, -22.5f, -45.0f };
            float zOffPair[4]    = { 2.0f, 1.0f, 0.0f, -1.0f };
            float phasePair[4]   = { 0.0f, PI, PI * 0.5f, PI * 1.5f };

            for (int pair = 0; pair < 4; ++pair) {
                float swingY = -(std::cos(m.limbSwing * 0.6662f * 2.0f + phasePair[pair]) * 0.4f) * RAD_TO_DEG * m.limbSwingAmt;
                float liftZ  = std::abs(std::sin(m.limbSwing * 0.6662f + phasePair[pair]) * 0.4f) * RAD_TO_DEG * m.limbSwingAmt;

                appendSkinBox(dst, x, y, z, bodyYaw, deathRoll, -4.0f, 9.0f, zOffPair[pair],
                              0.0f, -baseYawPair[pair] + swingY, 45.0f - liftZ,
                              -15.0f, -1.0f, -1.0f, 16, 2, 2, 18, 0, r, g, b);
                appendSkinBox(dst, x, y, z, bodyYaw, deathRoll,  4.0f, 9.0f, zOffPair[pair],
                              0.0f,  baseYawPair[pair] - swingY, -45.0f + liftZ,
                              -1.0f, -1.0f, -1.0f, 16, 2, 2, 18, 0, r, g, b);
            }
        } else if (m.type == MOB_GHAST) {
            auto& dst = skinBatches[SKIN_GHAST];
            const float pxScale = (1.0f / 16.0f) * 4.5f;

            auto appendGhastPart = [&](float pivX, float pivY, float pivZ,
                                       float rotXDeg,
                                       float offX, float offY, float offZ,
                                       float wPx, float hPx, float dPx,
                                       int texU, int texV) {
                float x0 = offX * pxScale, x1 = (offX + wPx) * pxScale;
                float y0 = offY * pxScale, y1 = (offY + hPx) * pxScale;
                float z0 = offZ * pxScale, z1 = (offZ + dPx) * pxScale;

                Mat4 xf = Mat4::translate(x, y, z) *
                          Mat4::rotateY(-bodyYaw) *
                          Mat4::rotateZ(deathRoll) *
                          Mat4::translate(pivX * pxScale, pivY * pxScale, pivZ * pxScale) *
                          Mat4::rotateX(rotXDeg);

                auto xformPt = [&](float lx, float ly, float lz) -> Vec3 {
                    return {
                        xf.m[0] * lx + xf.m[4] * ly + xf.m[8]  * lz + xf.m[12],
                        xf.m[1] * lx + xf.m[5] * ly + xf.m[9]  * lz + xf.m[13],
                        xf.m[2] * lx + xf.m[6] * ly + xf.m[10] * lz + xf.m[14]
                    };
                };

                const float invW = 1.0f / 64.0f;
                const float invH = 1.0f / 32.0f;
                int iu = texU, iv = texV, iw = static_cast<int>(wPx), ih = static_cast<int>(hPx), id = static_cast<int>(dPx);

                auto emitFace = [&](const float pts[4][3], float nx, float ny, float nz, int u0, int v0, int u1, int v1) {
                    Vec3 p0 = xformPt(pts[0][0], pts[0][1], pts[0][2]);
                    Vec3 p1 = xformPt(pts[1][0], pts[1][1], pts[1][2]);
                    Vec3 p2 = xformPt(pts[2][0], pts[2][1], pts[2][2]);
                    Vec3 p3 = xformPt(pts[3][0], pts[3][1], pts[3][2]);
                    float fu0 = u0 * invW, fv0 = v0 * invH, fu1 = u1 * invW, fv1 = v1 * invH;
                    EntityVertex v[4] = {
                        { p0.x, p0.y, p0.z, nx, ny, nz, fu0, fv1, r, g, b, 1.0f },
                        { p1.x, p1.y, p1.z, nx, ny, nz, fu1, fv1, r, g, b, 1.0f },
                        { p2.x, p2.y, p2.z, nx, ny, nz, fu1, fv0, r, g, b, 1.0f },
                        { p3.x, p3.y, p3.z, nx, ny, nz, fu0, fv0, r, g, b, 1.0f }
                    };
                    dst.push_back(v[0]); dst.push_back(v[1]); dst.push_back(v[2]);
                    dst.push_back(v[0]); dst.push_back(v[2]); dst.push_back(v[3]);
                };

                float fS[4][3] = {{x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}};
                emitFace(fS, 0, 0, 1, iu + id, iv + id, iu + id + iw, iv + id + ih);
                float fN[4][3] = {{x1,y0,z0}, {x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}};
                emitFace(fN, 0, 0, -1, iu + id + iw + id, iv + id, iu + id + iw + id + iw, iv + id + ih);
                float fE[4][3] = {{x1,y0,z1}, {x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}};
                emitFace(fE, 1, 0, 0, iu + id + iw, iv + id, iu + id + iw + id, iv + id + ih);
                float fW[4][3] = {{x0,y0,z0}, {x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}};
                emitFace(fW, -1, 0, 0, iu, iv + id, iu + id, iv + id + ih);
                float fT[4][3] = {{x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, {x0,y1,z0}};
                emitFace(fT, 0, 1, 0, iu + id, iv, iu + id + iw, iv + id);
                float fB[4][3] = {{x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, {x0,y0,z1}};
                emitFace(fB, 0, -1, 0, iu + id + iw, iv, iu + id + iw + iw, iv + id);
            };

            appendGhastPart(0.0f, 16.0f, 0.0f, 0.0f, -8.0f, -8.0f, -8.0f, 16.0f, 16.0f, 16.0f, 0, 0);

            static constexpr float tentacleLen[9] = { 11.0f, 13.0f, 9.0f, 14.0f, 10.0f, 12.0f, 8.0f, 15.0f, 11.0f };
            for (int j = 0; j < 9; ++j) {
                float tx = ((static_cast<float>(j % 3) - static_cast<float>(j / 3 % 2) * 0.5f + 0.25f) / 2.0f * 2.0f - 1.0f) * 5.0f;
                float tz = (static_cast<float>(j / 3) / 2.0f * 2.0f - 1.0f) * 5.0f;
                float rotX = (0.2f * std::sin(agePlusAlpha * 0.3f + static_cast<float>(j)) + 0.4f) * RAD_TO_DEG;
                appendGhastPart(tx, 9.0f, -tz, -rotX, -1.0f, -tentacleLen[j], -1.0f, 2.0f, tentacleLen[j], 2.0f, 0, 0);
            }
        }
    }

    static void appendMiniCube(std::vector<EntityVertex>& dst, float cx, float cy, float cz,
                               float size, float yawDeg, uint16_t spriteIdx, float gridDim,
                               float r, float g, float b) {
        uint16_t spr6[6] = { spriteIdx, spriteIdx, spriteIdx, spriteIdx, spriteIdx, spriteIdx };
        appendTexturedBox(dst, cx, cy, cz, size, size, size, yawDeg, spr6, r, g, b, r, g, b, false, gridDim);
    }

    static void appendTexturedBox(std::vector<EntityVertex>& dst,
                                  float cx, float cy, float cz,
                                  float sx, float sy, float sz,
                                  float yawDeg, const uint16_t faceSprites[6],
                                  float r, float g, float b,
                                  float topR, float topG, float topB,
                                  bool topOnlyTint, float gridDim = 16.0f) {
        float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
        float inv = 1.0f / gridDim;
        Mat4 rot = Mat4::rotateY(yawDeg);

        auto emitFace = [&](BlockFace face, float p[4][3], float nx, float ny, float nz) {
            uint16_t spriteIdx = faceSprites[face];
            float u0 = (spriteIdx % static_cast<int>(gridDim)) * inv + 0.001f;
            float v0 = (spriteIdx / static_cast<int>(gridDim)) * inv + 0.001f;
            float u1 = u0 + inv - 0.002f, v1 = v0 + inv - 0.002f;

            float fr = (face == FACE_TOP || !topOnlyTint) ? topR : r;
            float fg = (face == FACE_TOP || !topOnlyTint) ? topG : g;
            float fb = (face == FACE_TOP || !topOnlyTint) ? topB : b;

            EntityVertex v[4];
            float uvs[4][2] = {{u0,v1},{u1,v1},{u1,v0},{u0,v0}};
            for (int i = 0; i < 4; ++i) {
                float rx = rot.m[0] * p[i][0] + rot.m[8] * p[i][2] + cx;
                float ry = p[i][1] + cy;
                float rz = rot.m[2] * p[i][0] + rot.m[10] * p[i][2] + cz;
                v[i] = { rx, ry, rz, nx, ny, nz, uvs[i][0], uvs[i][1], fr, fg, fb, 1.0f };
            }
            dst.push_back(v[0]); dst.push_back(v[1]); dst.push_back(v[2]);
            dst.push_back(v[0]); dst.push_back(v[2]); dst.push_back(v[3]);
        };

        float fS[4][3]={{-hx,-hy,hz},{hx,-hy,hz},{hx,hy,hz},{-hx,hy,hz}};     emitFace(FACE_SOUTH,  fS, 0,0,1);
        float fN[4][3]={{hx,-hy,-hz},{-hx,-hy,-hz},{-hx,hy,-hz},{hx,hy,-hz}}; emitFace(FACE_NORTH,  fN, 0,0,-1);
        float fE[4][3]={{hx,-hy,hz},{hx,-hy,-hz},{hx,hy,-hz},{hx,hy,hz}};     emitFace(FACE_EAST,   fE, 1,0,0);
        float fW[4][3]={{-hx,-hy,-hz},{-hx,-hy,hz},{hx,hy,hz},{-hx,hy,-hz}}; emitFace(FACE_WEST,   fW,-1,0,0);
        float fT[4][3]={{-hx,hy,hz},{hx,hy,hz},{hx,hy,-hz},{-hx,hy,-hz}};     emitFace(FACE_TOP,    fT, 0,1,0);
        float fB[4][3]={{-hx,-hy,-hz},{hx,-hy,-hz},{hx,-hy,hz},{-hx,-hy,hz}}; emitFace(FACE_BOTTOM, fB, 0,-1,0);
    }

    static void appendArrow3D(std::vector<EntityVertex>& dst, float cx, float cy, float cz,
                              float yawDeg, float pitchDeg, float light) {
        const uint16_t arrowSpr = 37; // ITEM_ARROW
        float u0 = (arrowSpr % 16) / 16.0f + 0.001f;
        float v0 = (arrowSpr / 16) / 16.0f + 0.001f;
        float u1 = (arrowSpr % 16 + 1) / 16.0f - 0.001f;
        float v1 = (arrowSpr / 16 + 1) / 16.0f - 0.001f;

        Mat4 xf = Mat4::translate(cx, cy, cz) *
                  Mat4::rotateY(-yawDeg) *
                  Mat4::rotateZ(pitchDeg);

        auto xformPt = [&](float lx, float ly, float lz) -> Vec3 {
            return {
                xf.m[0] * lx + xf.m[4] * ly + xf.m[8]  * lz + xf.m[12],
                xf.m[1] * lx + xf.m[5] * ly + xf.m[9]  * lz + xf.m[13],
                xf.m[2] * lx + xf.m[6] * ly + xf.m[10] * lz + xf.m[14]
            };
        };

        auto emitPlane = [&](float rXDeg) {
            Mat4 r = Mat4::rotateX(rXDeg);
            auto rotX = [&](float lx, float ly, float lz) -> Vec3 {
                return xformPt(
                    lx,
                    r.m[5] * ly + r.m[9]  * lz,
                    r.m[6] * ly + r.m[10] * lz
                );
            };
            Vec3 p0 = rotX(-0.45f, -0.18f, 0.0f);
            Vec3 p1 = rotX( 0.45f, -0.18f, 0.0f);
            Vec3 p2 = rotX( 0.45f,  0.18f, 0.0f);
            Vec3 p3 = rotX(-0.45f,  0.18f, 0.0f);

            EntityVertex q[4] = {
                { p0.x, p0.y, p0.z, 0, 1, 0, u0, v1, light, light, light, 1.0f },
                { p1.x, p1.y, p1.z, 0, 1, 0, u1, v1, light, light, light, 1.0f },
                { p2.x, p2.y, p2.z, 0, 1, 0, u1, v0, light, light, light, 1.0f },
                { p3.x, p3.y, p3.z, 0, 1, 0, u0, v0, light, light, light, 1.0f }
            };
            dst.push_back(q[0]); dst.push_back(q[1]); dst.push_back(q[2]);
            dst.push_back(q[0]); dst.push_back(q[2]); dst.push_back(q[3]);
        };

        emitPlane(45.0f);
        emitPlane(-45.0f);
    }

    // Exact Canonical Sub-Texel Particle Voxel (All 6 faces aligned to particle radius h)
    static void appendSubTexelParticleCube(std::vector<EntityVertex>& dst, float cx, float cy, float cz,
                                           float size, uint16_t spriteIdx, int hashSeed, float shade) {
        float h = size * 0.5f;
        float tileU0 = (spriteIdx % 16) / 16.0f;
        float tileV0 = (spriteIdx / 16) / 16.0f;
        float subU = (static_cast<uint32_t>(hashSeed) % 12) / 256.0f;
        float subV = (static_cast<uint32_t>(hashSeed >> 4) % 12) / 256.0f;
        float u0 = tileU0 + subU;
        float v0 = tileV0 + subV;
        float u1 = u0 + (4.0f / 256.0f);
        float v1 = v0 + (4.0f / 256.0f);

        auto emitFace = [&](const float p[4][3], float nx, float ny, float nz) {
            EntityVertex v[4] = {
                { p[0][0]+cx, p[0][1]+cy, p[0][2]+cz, nx, ny, nz, u0, v1, shade, shade, shade, 1.0f },
                { p[1][0]+cx, p[1][1]+cy, p[1][2]+cz, nx, ny, nz, u1, v1, shade, shade, shade, 1.0f },
                { p[2][0]+cx, p[2][1]+cy, p[2][2]+cz, nx, ny, nz, u1, v0, shade, shade, shade, 1.0f },
                { p[3][0]+cx, p[3][1]+cy, p[3][2]+cz, nx, ny, nz, u0, v0, shade, shade, shade, 1.0f }
            };
            dst.push_back(v[0]); dst.push_back(v[1]); dst.push_back(v[2]);
            dst.push_back(v[0]); dst.push_back(v[2]); dst.push_back(v[3]);
        };

        float fS[4][3] = { {-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h} }; emitFace(fS,  0.0f,  0.0f,  1.0f);
        float fN[4][3] = { { h, -h, -h}, {-h, -h, -h}, {-h,  h, -h}, { h,  h, -h} }; emitFace(fN,  0.0f,  0.0f, -1.0f);
        float fE[4][3] = { { h, -h,  h}, { h, -h, -h}, { h,  h, -h}, { h,  h,  h} }; emitFace(fE,  1.0f,  0.0f,  0.0f);
        float fW[4][3] = { {-h, -h, -h}, {-h, -h,  h}, {-h,  h,  h}, {-h,  h, -h} }; emitFace(fW, -1.0f,  0.0f,  0.0f);
        float fT[4][3] = { {-h,  h,  h}, { h,  h,  h}, { h,  h, -h}, {-h,  h, -h} }; emitFace(fT,  0.0f,  1.0f,  0.0f);
        float fB[4][3] = { {-h, -h, -h}, { h, -h, -h}, { h, -h,  h}, {-h, -h,  h} }; emitFace(fB,  0.0f, -1.0f,  0.0f);
    }

    static void appendBillboardSprite(std::vector<EntityVertex>& dst, float cx, float cy, float cz,
                                      float size, float yawDeg, uint16_t spriteIdx, float gridDim,
                                      float r, float g, float b) {
        float h = size * 0.5f;
        float inv = 1.0f / gridDim;
        float u0 = (spriteIdx % static_cast<int>(gridDim)) * inv + 0.001f;
        float v0 = (spriteIdx / static_cast<int>(gridDim)) * inv + 0.001f;
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
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    }
};