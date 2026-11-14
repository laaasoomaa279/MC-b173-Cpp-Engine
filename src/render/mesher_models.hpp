#pragma once
#include "render/mesher_cubes.hpp"
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>

// ============================================================================
// Minecraft Beta 1.7.3 Special Block Models (RenderBlocks.java 1:1 Quirks)
// ============================================================================
// Reproduces every classic Beta 1.7.3 non-cubic geometry & visual quirk:
//   - Torches & Redstone Torches: Exact 0.4 shear tilt on walls with unclipped
//     top flame tip!
//   - Water & Lava Fluids: 4-corner height averaging (getFluidCornerHeight)
//     for smooth diagonal waterfall slopes & submerged face culling.
//   - X-Cross Flora & Tall Grass: Deterministic Beta pseudo-random X/Z jitter
//     hash so flowers and tall grass sit organically off-center!
//   - Wheat Crops: Authentic 4-plane vertical grid at X=4/12 and Z=4/12.
//   - Sub-Voxel 1/16 Boxes: Pistons (29, 33, 34), Stairs (53, 67), Slabs (44),
//     Fences (85), Doors (64, 71), Trapdoors (96), Rails (27, 28, 66),
//     Redstone Wire (55) with power tint, Repeaters (93, 94), Buttons & Levers.
// ============================================================================

class BetaModelMesher {
public:
    static void buildSpecialModels(const WorldMap& world, int32_t cx, int32_t secY, int32_t cz,
                                   std::vector<uint64_t>& outOpaque,
                                   std::vector<uint64_t>& outTranslucent) {
        const ChunkColumn* col = world.getReadyChunk(cx, cz);
        if (!col) return;
        const ChunkSection& sec = col->sections[secY];
        int baseX = cx * 16, baseY = secY * 16, baseZ = cz * 16;

        SubBoxList boxList;

        for (int ly = 0; ly < 16; ++ly) {
            for (int lz = 0; lz < 16; ++lz) {
                for (int lx = 0; lx < 16; ++lx) {
                    uint16_t st = sec.getBlock(lx, ly, lz);
                    if (st == 0) continue;

                    const BlockDef& def = world.registry.get(st);
                    if (def.shape == SHAPE_NONE || def.shape == SHAPE_CUBE) continue;

                    int wx = baseX + lx, wy = baseY + ly, wz = baseZ + lz;
                    uint8_t skyL = world.getSkyLightAt(wx, wy, wz);
                    uint8_t blkL = std::max(def.lightEmission, world.getBlockLightAt(wx, wy, wz));

                    if (def.shape == SHAPE_TORCH) {
                        emitTorchModel(st, (float)lx, (float)ly, (float)lz, skyL, blkL, outOpaque);
                    } else if (def.shape == SHAPE_CROSS) {
                        emitCrossFloraModel(st, def, wx, wy, wz, (float)lx, (float)ly, (float)lz, skyL, blkL, outOpaque);
                    } else if (def.shape == SHAPE_CROP) {
                        emitWheatCropModel(st, (float)lx, (float)ly, (float)lz, skyL, blkL, outOpaque);
                    } else if (def.shape == SHAPE_FLUID) {
                        bool isWater = (((st >> 4) & 0xFF) == 8 || ((st >> 4) & 0xFF) == 9);
                        emitFluidModel(world, st, wx, wy, wz, (float)lx, (float)ly, (float)lz, skyL, blkL,
                                       isWater ? outTranslucent : outOpaque);
                    } else if (def.shape == SHAPE_FIRE || def.shape == SHAPE_PORTAL) {
                        emitPaneOrFireModel(st, def, (float)lx, (float)ly, (float)lz, skyL, blkL,
                                            (def.shape == SHAPE_PORTAL) ? outTranslucent : outOpaque);
                    } else {
                        // All 1/16 Sub-Box blocks (Pistons, Stairs, Slabs, Fences, Doors, Repeaters, Redstone, Rails, Cake, Bed)
                        world.registry.getSubBoxes(st, wx, wy, wz, [&](int qx, int qy, int qz) {
                            return world.getBlockAt(qx, qy, qz);
                        }, boxList);

                        uint8_t power = (uint8_t)(st & 0x0F);
                        for (int b = 0; b < boxList.count; ++b) {
                            emitSubBox(boxList.boxes[b], (float)lx, (float)ly, (float)lz, skyL, blkL, power, outOpaque);
                        }
                    }
                }
            }
        }
    }

private:
    static void emitQuad(std::vector<uint64_t>& dst, const float p[4][3], uint8_t face,
                         uint8_t skyL, uint8_t blkL, uint16_t sprite, uint8_t tint, uint8_t extra = 0) {
        uint64_t v[4];
        for (int i = 0; i < 4; ++i) {
            v[i] = packChunkVertex(p[i][0], p[i][1], p[i][2], face, 3, skyL, blkL,
                                   sprite, 1, 1, tint, extra, (uint8_t)i);
        }
        dst.push_back(v[0]); dst.push_back(v[1]); dst.push_back(v[2]);
        dst.push_back(v[0]); dst.push_back(v[2]); dst.push_back(v[3]);
    }

    // Beta 1.7.3 RenderBlocks.renderBlockTorch: exact 0.4 wall shear tilt with full 10/16 height
    static void emitTorchModel(uint16_t st, float lx, float ly, float lz,
                               uint8_t skyL, uint8_t blkL, std::vector<uint64_t>& dst) {
        uint8_t id   = (uint8_t)((st >> 4) & 0xFF);
        uint8_t meta = (uint8_t)(st & 0x0F);
        uint16_t spr = (id == 75) ? SPR_REDSTONE_TORCH_OFF : ((id == 76) ? SPR_REDSTONE_TORCH_ON : SPR_TORCH);

        float cx = lx + 0.5f, cz = lz + 0.5f, y0 = ly, y1 = ly + 0.625f; // 10/16 block height
        float shearX = 0.0f, shearZ = 0.0f;

        if (meta == 1)      { cx = lx + 0.1f; y0 = ly + 0.2f; y1 = ly + 0.825f; shearX =  0.25f; }
        else if (meta == 2) { cx = lx + 0.9f; y0 = ly + 0.2f; y1 = ly + 0.825f; shearX = -0.25f; }
        else if (meta == 3) { cz = lz + 0.1f; y0 = ly + 0.2f; y1 = ly + 0.825f; shearZ =  0.25f; }
        else if (meta == 4) { cz = lz + 0.9f; y0 = ly + 0.2f; y1 = ly + 0.825f; shearZ = -0.25f; }

        float hw = 0.0625f; // 1/16 half-width (2x2 pixel shaft)
        // East & West vertical quads
        float pE[4][3] = {{cx+hw, y0, cz+0.5f}, {cx+hw, y0, cz-0.5f}, {cx+hw+shearX, y1, cz-0.5f+shearZ}, {cx+hw+shearX, y1, cz+0.5f+shearZ}};
        float pW[4][3] = {{cx-hw, y0, cz-0.5f}, {cx-hw, y0, cz+0.5f}, {cx-hw+shearX, y1, cz+0.5f+shearZ}, {cx-hw+shearX, y1, cz-0.5f+shearZ}};
        float pS[4][3] = {{cx-0.5f, y0, cz+hw}, {cx+0.5f, y0, cz+hw}, {cx+0.5f+shearX, y1, cz+hw+shearZ}, {cx-0.5f+shearX, y1, cz+hw+shearZ}};
        float pN[4][3] = {{cx+0.5f, y0, cz-hw}, {cx-0.5f, y0, cz-hw}, {cx-0.5f+shearX, y1, cz-hw+shearZ}, {cx+0.5f+shearX, y1, cz-hw+shearZ}};

        emitQuad(dst, pE, FACE_EAST,  skyL, blkL, spr, TINT_NONE);
        emitQuad(dst, pW, FACE_WEST,  skyL, blkL, spr, TINT_NONE);
        emitQuad(dst, pS, FACE_SOUTH, skyL, blkL, spr, TINT_NONE);
        emitQuad(dst, pN, FACE_NORTH, skyL, blkL, spr, TINT_NONE);
    }

    // Beta 1.7.3 X-Cross plants with deterministic world-coordinate pseudo-random X/Z offset
    static void emitCrossFloraModel(uint16_t st, const BlockDef& def, int wx, int wy, int wz,
                                    float lx, float ly, float lz, uint8_t skyL, uint8_t blkL,
                                    std::vector<uint64_t>& dst) {
        uint8_t id = (uint8_t)((st >> 4) & 0xFF);
        uint16_t spr = def.faceTex[FACE_NORTH];
        if (id == 31) {
            uint8_t m = st & 0x0F;
            spr = (m == 0) ? SPR_DEAD_BUSH : ((m == 2) ? SPR_FERN : SPR_TALL_GRASS);
        } else if (id == 6) {
            uint8_t m = st & 0x03;
            spr = (m == 1) ? SPR_SAPLING_SPRUCE : ((m == 2) ? SPR_SAPLING_BIRCH : SPR_SAPLING_OAK);
        }

        // Authentic Notch Beta 1.7.3 deterministic grass/flower coordinate jitter
        float jx = 0.0f, jz = 0.0f;
        if (id == 31 || id == 37 || id == 38) {
            int64_t h = (int64_t)(wx * 3129871) ^ ((int64_t)wz * 116129781LL) ^ (int64_t)wy;
            h = h * h * 42317861LL + h * 11LL;
            jx = (((float)((h >> 16) & 15) / 15.0f) - 0.5f) * 0.5f;
            jz = (((float)((h >> 24) & 15) / 15.0f) - 0.5f) * 0.5f;
        }

        float x0 = lx + 0.15f + jx, x1 = lx + 0.85f + jx;
        float z0 = lz + 0.15f + jz, z1 = lz + 0.85f + jz;
        float y0 = ly, y1 = ly + 0.90f;

        float d1A[4][3] = {{x0, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z0}};
        float d1B[4][3] = {{x1, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z1}};
        float d2A[4][3] = {{x0, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z1}};
        float d2B[4][3] = {{x1, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z0}};

        emitQuad(dst, d1A, FACE_TOP, skyL, blkL, spr, def.tintMode);
        emitQuad(dst, d1B, FACE_TOP, skyL, blkL, spr, def.tintMode);
        emitQuad(dst, d2A, FACE_TOP, skyL, blkL, spr, def.tintMode);
        emitQuad(dst, d2B, FACE_TOP, skyL, blkL, spr, def.tintMode);
    }

    // Beta 1.7.3 RenderBlocks.renderBlockCrops: 4 parallel vertical planes at 4/16 and 12/16
    static void emitWheatCropModel(uint16_t st, float lx, float ly, float lz,
                                   uint8_t skyL, uint8_t blkL, std::vector<uint64_t>& dst) {
        uint16_t spr = (uint16_t)(SPR_WHEAT_0 + (st & 0x07));
        float y0 = ly - 0.0625f, y1 = ly + 0.9375f;
        const float offs[2] = { 0.25f, 0.75f };
        for (float o : offs) {
            float p1[4][3] = {{lx+o, y0, lz}, {lx+o, y0, lz+1}, {lx+o, y1, lz+1}, {lx+o, y1, lz}};
            float p2[4][3] = {{lx+o, y0, lz+1}, {lx+o, y0, lz}, {lx+o, y1, lz}, {lx+o, y1, lz+1}};
            float p3[4][3] = {{lx, y0, lz+o}, {lx+1, y0, lz+o}, {lx+1, y1, lz+o}, {lx, y1, lz+o}};
            float p4[4][3] = {{lx+1, y0, lz+o}, {lx, y0, lz+o}, {lx, y1, lz+o}, {lx+1, y1, lz+o}};
            emitQuad(dst, p1, FACE_TOP, skyL, blkL, spr, TINT_NONE);
            emitQuad(dst, p2, FACE_TOP, skyL, blkL, spr, TINT_NONE);
            emitQuad(dst, p3, FACE_TOP, skyL, blkL, spr, TINT_NONE);
            emitQuad(dst, p4, FACE_TOP, skyL, blkL, spr, TINT_NONE);
        }
    }

    // Smooth 4-corner fluid height calculation (Water 8/9 & Lava 10/11)
    static float getFluidCornerHeight(const WorldMap& world, int wx, int wy, int wz, bool isLava) {
        int samples = 0;
        float sum = 0.0f;
        for (int dz = -1; dz <= 0; ++dz) {
            for (int dx = -1; dx <= 0; ++dx) {
                uint16_t above = world.getBlockAt(wx + dx, wy + 1, wz + dz);
                uint8_t aId = (uint8_t)((above >> 4) & 0xFF);
                if ((!isLava && (aId == 8 || aId == 9)) || (isLava && (aId == 10 || aId == 11))) {
                    return 1.0f;
                }
                uint16_t st = world.getBlockAt(wx + dx, wy, wz + dz);
                uint8_t id  = (uint8_t)((st >> 4) & 0xFF);
                if ((!isLava && (id == 8 || id == 9)) || (isLava && (id == 10 || id == 11))) {
                    uint8_t lvl = st & 0x0F;
                    if (lvl >= 8 || lvl == 0) {
                        sum += 0.8888f * 10.0f;
                        samples += 10;
                    } else {
                        sum += (1.0f - (float)(lvl + 1) / 9.0f);
                        samples++;
                    }
                } else if (!world.registry.get(st).isSolid) {
                    samples++;
                }
            }
        }
        return (samples > 0) ? (sum / (float)samples) : 0.8888f;
    }

    static void emitFluidModel(const WorldMap& world, uint16_t st, int wx, int wy, int wz,
                               float lx, float ly, float lz, uint8_t skyL, uint8_t blkL,
                               std::vector<uint64_t>& dst) {
        uint8_t id = (uint8_t)((st >> 4) & 0xFF);
        bool isLava = (id == 10 || id == 11);
        uint16_t spr = isLava ? SPR_LAVA_STILL : SPR_WATER_STILL;

        auto isSameFluid = [&](int qx, int qy, int qz) -> bool {
            uint8_t nid = (uint8_t)((world.getBlockAt(qx, qy, qz) >> 4) & 0xFF);
            return isLava ? (nid == 10 || nid == 11) : (nid == 8 || nid == 9);
        };

        float h00 = getFluidCornerHeight(world, wx,     wy, wz,     isLava);
        float h10 = getFluidCornerHeight(world, wx + 1, wy, wz,     isLava);
        float h11 = getFluidCornerHeight(world, wx + 1, wy, wz + 1, isLava);
        float h01 = getFluidCornerHeight(world, wx,     wy, wz + 1, isLava);

        if (!isSameFluid(wx, wy + 1, wz)) {
            float top[4][3] = {
                {lx,     ly + h01, lz + 1},
                {lx + 1, ly + h11, lz + 1},
                {lx + 1, ly + h10, lz},
                {lx,     ly + h00, lz}
            };
            emitQuad(dst, top, FACE_TOP, skyL, blkL, spr, TINT_NONE);
        }

        if (!isSameFluid(wx, wy - 1, wz) && !world.registry.get(world.getBlockAt(wx, wy - 1, wz)).isOpaque) {
            float bot[4][3] = {{lx, ly, lz}, {lx+1, ly, lz}, {lx+1, ly, lz+1}, {lx, ly, lz+1}};
            emitQuad(dst, bot, FACE_BOTTOM, skyL, blkL, spr, TINT_NONE);
        }

        if (!isSameFluid(wx + 1, wy, wz) && !world.registry.get(world.getBlockAt(wx + 1, wy, wz)).isOpaque) {
            float p[4][3] = {{lx+1, ly, lz+1}, {lx+1, ly, lz}, {lx+1, ly+h10, lz}, {lx+1, ly+h11, lz+1}};
            emitQuad(dst, p, FACE_EAST, skyL, blkL, spr, TINT_NONE);
        }
        if (!isSameFluid(wx - 1, wy, wz) && !world.registry.get(world.getBlockAt(wx - 1, wy, wz)).isOpaque) {
            float p[4][3] = {{lx, ly, lz}, {lx, ly, lz+1}, {lx, ly+h01, lz+1}, {lx, ly+h00, lz}};
            emitQuad(dst, p, FACE_WEST, skyL, blkL, spr, TINT_NONE);
        }
        if (!isSameFluid(wx, wy, wz + 1) && !world.registry.get(world.getBlockAt(wx, wy, wz + 1)).isOpaque) {
            float p[4][3] = {{lx, ly, lz+1}, {lx+1, ly, lz+1}, {lx+1, ly+h11, lz+1}, {lx, ly+h01, lz+1}};
            emitQuad(dst, p, FACE_SOUTH, skyL, blkL, spr, TINT_NONE);
        }
        if (!isSameFluid(wx, wy, wz - 1) && !world.registry.get(world.getBlockAt(wx, wy, wz - 1)).isOpaque) {
            float p[4][3] = {{lx+1, ly, lz}, {lx, ly, lz}, {lx, ly+h00, lz}, {lx+1, ly+h10, lz}};
            emitQuad(dst, p, FACE_NORTH, skyL, blkL, spr, TINT_NONE);
        }
    }

    static void emitPaneOrFireModel(uint16_t st, const BlockDef& def, float lx, float ly, float lz,
                                    uint8_t skyL, uint8_t blkL, std::vector<uint64_t>& dst) {
        (void)st;
        uint16_t spr = def.faceTex[FACE_NORTH];
        if (def.shape == SHAPE_PORTAL) {
            float p1[4][3] = {{lx, ly, lz+0.375f}, {lx+1, ly, lz+0.375f}, {lx+1, ly+1, lz+0.375f}, {lx, ly+1, lz+0.375f}};
            float p2[4][3] = {{lx+1, ly, lz+0.625f}, {lx, ly, lz+0.625f}, {lx, ly+1, lz+0.625f}, {lx+1, ly+1, lz+0.625f}};
            emitQuad(dst, p1, FACE_SOUTH, skyL, blkL, spr, TINT_NONE);
            emitQuad(dst, p2, FACE_NORTH, skyL, blkL, spr, TINT_NONE);
        } else {
            float p1[4][3] = {{lx, ly, lz}, {lx+1, ly, lz+1}, {lx+1, ly+1, lz+1}, {lx, ly+1, lz}};
            float p2[4][3] = {{lx, ly, lz+1}, {lx+1, ly, lz}, {lx+1, ly+1, lz}, {lx, ly+1, lz+1}};
            emitQuad(dst, p1, FACE_TOP, skyL, blkL, spr, TINT_NONE);
            emitQuad(dst, p2, FACE_TOP, skyL, blkL, spr, TINT_NONE);
        }
    }

    static void emitSubBox(const SubBox& box, float lx, float ly, float lz,
                           uint8_t skyL, uint8_t blkL, uint8_t extraData, std::vector<uint64_t>& dst) {
        float x0 = lx + box.minX * 0.0625f, y0 = ly + box.minY * 0.0625f, z0 = lz + box.minZ * 0.0625f;
        float x1 = lx + box.maxX * 0.0625f, y1 = ly + box.maxY * 0.0625f, z1 = lz + box.maxZ * 0.0625f;

        float pE[4][3] = {{x1,y0,z1}, {x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}};
        float pW[4][3] = {{x0,y0,z0}, {x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}};
        float pT[4][3] = {{x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, {x0,y1,z0}};
        float pB[4][3] = {{x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, {x0,y0,z1}};
        float pS[4][3] = {{x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}};
        float pN[4][3] = {{x1,y0,z0}, {x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}};

        emitQuad(dst, pE, FACE_EAST,   skyL, blkL, box.faceTex[FACE_EAST],   box.tint, extraData);
        emitQuad(dst, pW, FACE_WEST,   skyL, blkL, box.faceTex[FACE_WEST],   box.tint, extraData);
        emitQuad(dst, pT, FACE_TOP,    skyL, blkL, box.faceTex[FACE_TOP],    box.tint, extraData);
        emitQuad(dst, pB, FACE_BOTTOM, skyL, blkL, box.faceTex[FACE_BOTTOM], box.tint, extraData);
        emitQuad(dst, pS, FACE_SOUTH,  skyL, blkL, box.faceTex[FACE_SOUTH],  box.tint, extraData);
        emitQuad(dst, pN, FACE_NORTH,  skyL, blkL, box.faceTex[FACE_NORTH],  box.tint, extraData);
    }
};

// Unified Mesher Entry Point used by WorkerPool
class BetaMesher {
public:
    static void buildSectionMesh(const WorldMap& world, int32_t cx, int32_t secY, int32_t cz,
                                 std::vector<uint64_t>& outOpaque,
                                 std::vector<uint64_t>& outTranslucent,
                                 bool smoothLighting = true) {
        outOpaque.clear();
        outTranslucent.clear();
        BetaCubeMesher::buildFullCubesGreedy(world, cx, secY, cz, smoothLighting, outOpaque, outTranslucent);
        BetaModelMesher::buildSpecialModels(world, cx, secY, cz, outOpaque, outTranslucent);
    }
};