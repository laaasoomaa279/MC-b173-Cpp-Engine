#pragma once
#include "render/mesher_cubes.hpp"
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>

// ============================================================================
// CANONICAL NON-CUBIC SUB-VOXEL & FLUID TESSALATOR SPECIFICATION (SPEC-2011)
// ============================================================================
// Technical Subsystem Architecture:
// 1. Thermodynamic Fluid Surface Tessellation (Water & Lava Manifolds):
//    - Elevation Metric & Corner Weighting:
//        Each column corner samples a 2x2 horizontal neighborhood of 4 vertical
//        block columns sharing that corner vertex:
//          * If any neighbor column contains fluid directly above (Y + 1),
//            the corner height is locked to 1.0F (continuous vertical waterfall).
//          * Stable fluid sources (decay level 0 or falling fluid >= 8) contribute
//            a 10x multiplier weight to prevent premature surface sagging.
//          * Neighboring non-solid air blocks contribute a normalized decay penalty
//            of 1.0F to create natural convex discharge slopes towards drops.
//          * Final Corner Height: H = 1.0F - (sum(decay_weights) / sum(sample_weights)).
//    - Boundary Occlusion Rules: Lateral fluid faces are suppressed against
//      identical fluid cells, solid opaque cubes, and frozen ice (ID 79),
//      eliminating dark seam artifacts across cryospheric biome borders.
// 2. Directional Luminary Geometry (Wall & Floor Torches):
//    - Floor mounting: Vertical 2x2 sub-pixel shaft centered at (0.5, 0.5) with
//      height 10/16 (0.625F).
//    - Wall mounting: Base anchored at Y + 0.2F against the wall surface, leaning
//      0.25F outward into the chamber. Emits all 4 lateral quads plus top flame cap
//      and bottom timber faces.
// 3. Dual-Axis Dimensional Aperture Panes (Nether Portals):
//    - Orientation metadata bit 0x02 designates longitudinal alignment along Z,
//      while bit 0x01 aligns along X, projecting dual-sided translucent quads.
// 4. Botanical & Agricultural Planar Arrays:
//    - Cross Flora (Saplings, Flowers, Tall Grass): Dual diagonal intersecting
//      planes with deterministic pseudo-random spatial offset (jitter hash) to
//      eliminate artificial grid alignment.
//    - Agricultural Wheat: 4 parallel vertical planar quads stationed at lateral
//      offsets 4/16 and 12/16 along X and Z.
// 5. Quantized Sub-Box Culling (Slabs, Stairs, Fences, Pistons, Cake, Beds):
//    - Evaluates peripheral face occlusion against adjacent opaque blocks to
//      suppress internal non-visible geometry and eliminate Z-fighting.
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
                        emitTorchModel(st, static_cast<float>(lx), static_cast<float>(ly), static_cast<float>(lz), skyL, blkL, outOpaque);
                    } else if (def.shape == SHAPE_CROSS) {
                        emitCrossFloraModel(st, def, wx, wy, wz, static_cast<float>(lx), static_cast<float>(ly), static_cast<float>(lz), skyL, blkL, outOpaque);
                    } else if (def.shape == SHAPE_CROP) {
                        emitWheatCropModel(st, static_cast<float>(lx), static_cast<float>(ly), static_cast<float>(lz), skyL, blkL, outOpaque);
                    } else if (def.shape == SHAPE_FLUID) {
                        bool isWater = (((st >> 4) & 0xFF) == 8 || ((st >> 4) & 0xFF) == 9);
                        emitFluidModel(world, st, wx, wy, wz, static_cast<float>(lx), static_cast<float>(ly), static_cast<float>(lz), skyL, blkL,
                                       isWater ? outTranslucent : outOpaque);
                    } else if (def.shape == SHAPE_FIRE || def.shape == SHAPE_PORTAL) {
                        emitPaneOrFireModel(st, def, static_cast<float>(lx), static_cast<float>(ly), static_cast<float>(lz), skyL, blkL,
                                            (def.shape == SHAPE_PORTAL) ? outTranslucent : outOpaque);
                    } else {
                        world.registry.getSubBoxes(st, wx, wy, wz, [&](int qx, int qy, int qz) {
                            return world.getBlockAt(qx, qy, qz);
                        }, boxList);

                        uint8_t power = static_cast<uint8_t>(st & 0x0F);
                        for (int b = 0; b < boxList.count; ++b) {
                            emitSubBox(boxList.boxes[b], static_cast<float>(lx), static_cast<float>(ly), static_cast<float>(lz),
                                       skyL, blkL, power, outOpaque, &world, wx, wy, wz);
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
                                   sprite, 1, 1, tint, extra, static_cast<uint8_t>(i));
        }
        dst.push_back(v[0]); dst.push_back(v[1]); dst.push_back(v[2]);
        dst.push_back(v[0]); dst.push_back(v[2]); dst.push_back(v[3]);
    }

    // Directional Luminary Torch: 4 lateral quads, top flame cap, and bottom timber face
    static void emitTorchModel(uint16_t st, float lx, float ly, float lz,
                               uint8_t skyL, uint8_t blkL, std::vector<uint64_t>& dst) {
        uint8_t id   = static_cast<uint8_t>((st >> 4) & 0xFF);
        uint8_t meta = static_cast<uint8_t>(st & 0x0F);
        uint16_t spr = (id == 75) ? SPR_REDSTONE_TORCH_OFF : ((id == 76) ? SPR_REDSTONE_TORCH_ON : SPR_TORCH);

        float cx = lx + 0.5f, cz = lz + 0.5f, y0 = ly, y1 = ly + 0.625f;
        float shearX = 0.0f, shearZ = 0.0f;

        if (meta == 1)      { cx = lx + 0.1f; y0 = ly + 0.2f; y1 = ly + 0.825f; shearX =  0.25f; }
        else if (meta == 2) { cx = lx + 0.9f; y0 = ly + 0.2f; y1 = ly + 0.825f; shearX = -0.25f; }
        else if (meta == 3) { cz = lz + 0.1f; y0 = ly + 0.2f; y1 = ly + 0.825f; shearZ =  0.25f; }
        else if (meta == 4) { cz = lz + 0.9f; y0 = ly + 0.2f; y1 = ly + 0.825f; shearZ = -0.25f; }

        float hw = 0.0625f; // 1/16th half-width (2x2 sub-pixel post)

        // Lateral faces
        float pE[4][3] = {{cx+hw, y0, cz+0.5f}, {cx+hw, y0, cz-0.5f}, {cx+hw+shearX, y1, cz-0.5f+shearZ}, {cx+hw+shearX, y1, cz+0.5f+shearZ}};
        float pW[4][3] = {{cx-hw, y0, cz-0.5f}, {cx-hw, y0, cz+0.5f}, {cx-hw+shearX, y1, cz+0.5f+shearZ}, {cx-hw+shearX, y1, cz-0.5f+shearZ}};
        float pS[4][3] = {{cx-0.5f, y0, cz+hw}, {cx+0.5f, y0, cz+hw}, {cx+0.5f+shearX, y1, cz+hw+shearZ}, {cx-0.5f+shearX, y1, cz+hw+shearZ}};
        float pN[4][3] = {{cx+0.5f, y0, cz-hw}, {cx-0.5f, y0, cz-hw}, {cx-0.5f+shearX, y1, cz-hw+shearZ}, {cx+0.5f+shearX, y1, cz-hw+shearZ}};

        emitQuad(dst, pE, FACE_EAST,  skyL, blkL, spr, TINT_NONE);
        emitQuad(dst, pW, FACE_WEST,  skyL, blkL, spr, TINT_NONE);
        emitQuad(dst, pS, FACE_SOUTH, skyL, blkL, spr, TINT_NONE);
        emitQuad(dst, pN, FACE_NORTH, skyL, blkL, spr, TINT_NONE);

        // Top flame cap and bottom timber face (2x2 sub-pixels)
        float topX = cx + shearX, topZ = cz + shearZ;
        float pT[4][3] = {{topX-hw, y1, topZ+hw}, {topX+hw, y1, topZ+hw}, {topX+hw, y1, topZ-hw}, {topX-hw, y1, topZ-hw}};
        float pB[4][3] = {{cx-hw,   y0, cz-hw},   {cx+hw,   y0, cz-hw},   {cx+hw,   y0, cz+hw},   {cx-hw,   y0, cz+hw}};

        emitQuad(dst, pT, FACE_TOP,    skyL, blkL, spr, TINT_NONE);
        emitQuad(dst, pB, FACE_BOTTOM, skyL, blkL, spr, TINT_NONE);
    }

    // Cross Flora with deterministic spatial jitter
    static void emitCrossFloraModel(uint16_t st, const BlockDef& def, int wx, int wy, int wz,
                                    float lx, float ly, float lz, uint8_t skyL, uint8_t blkL,
                                    std::vector<uint64_t>& dst) {
        uint8_t id = static_cast<uint8_t>((st >> 4) & 0xFF);
        uint16_t spr = def.faceTex[FACE_NORTH];
        if (id == 31) {
            uint8_t m = st & 0x0F;
            spr = (m == 0) ? SPR_DEAD_BUSH : ((m == 2) ? SPR_FERN : SPR_TALL_GRASS);
        } else if (id == 6) {
            uint8_t m = st & 0x03;
            spr = (m == 1) ? SPR_SAPLING_SPRUCE : ((m == 2) ? SPR_SAPLING_BIRCH : SPR_SAPLING_OAK);
        }

        float jx = 0.0f, jz = 0.0f;
        if (id == 31 || id == 37 || id == 38) {
            int64_t h = static_cast<int64_t>(wx * 3129871) ^ (static_cast<int64_t>(wz) * 116129781LL) ^ static_cast<int64_t>(wy);
            h = h * h * 42317861LL + h * 11LL;
            jx = ((static_cast<float>((h >> 16) & 15) / 15.0f) - 0.5f) * 0.5f;
            jz = ((static_cast<float>((h >> 24) & 15) / 15.0f) - 0.5f) * 0.5f;
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

    // Agricultural Crop Lattice: 4 vertical planes along X and Z
    static void emitWheatCropModel(uint16_t st, float lx, float ly, float lz,
                                   uint8_t skyL, uint8_t blkL, std::vector<uint64_t>& dst) {
        uint16_t spr = static_cast<uint16_t>(SPR_WHEAT_0 + (st & 0x07));
        float y0 = ly - 0.0625f, y1 = ly + 0.9375f;
        const float offs[2] = { 0.25f, 0.75f };
        for (float o : offs) {
            float p1[4][3] = {{lx+o, y0, lz},   {lx+o, y0, lz+1}, {lx+o, y1, lz+1}, {lx+o, y1, lz}};
            float p2[4][3] = {{lx+o, y0, lz+1}, {lx+o, y0, lz},   {lx+o, y1, lz},   {lx+o, y1, lz+1}};
            float p3[4][3] = {{lx,   y0, lz+o}, {lx+1, y0, lz+o}, {lx+1, y1, lz+o}, {lx,   y1, lz+o}};
            float p4[4][3] = {{lx+1, y0, lz+o}, {lx,   y0, lz+o}, {lx,   y1, lz+o}, {lx+1, y1, lz+o}};
            emitQuad(dst, p1, FACE_EAST,  skyL, blkL, spr, TINT_NONE);
            emitQuad(dst, p2, FACE_WEST,  skyL, blkL, spr, TINT_NONE);
            emitQuad(dst, p3, FACE_SOUTH, skyL, blkL, spr, TINT_NONE);
            emitQuad(dst, p4, FACE_NORTH, skyL, blkL, spr, TINT_NONE);
        }
    }

    // Exact Canonical Corner Fluid Elevation Metric (Resolves 2x2 column shared vertices)
    static float getFluidCornerHeight(const WorldMap& world, int wx, int wy, int wz, bool isLava) {
        int totalWeight = 0;
        float decaySum  = 0.0f;

        auto isSame = [isLava](uint8_t qId) -> bool {
            return isLava ? (qId == 10 || qId == 11) : (qId == 8 || qId == 9);
        };

        for (int dz = -1; dz <= 0; ++dz) {
            for (int dx = -1; dx <= 0; ++dx) {
                int nx = wx + dx, nz = wz + dz;
                uint16_t aboveSt = world.getBlockAt(nx, wy + 1, nz);
                uint8_t  aboveId = static_cast<uint8_t>((aboveSt >> 4) & 0xFF);
                if (isSame(aboveId)) {
                    return 1.0f; // Continuous waterfall cascade
                }

                uint16_t st = world.getBlockAt(nx, wy, nz);
                uint8_t  id = static_cast<uint8_t>((st >> 4) & 0xFF);
                if (isSame(id)) {
                    uint8_t meta = static_cast<uint8_t>(st & 0x0F);
                    int decay = (meta >= 8) ? 0 : static_cast<int>(meta);
                    float percent = static_cast<float>(decay + 1) / 9.0f;
                    if (meta >= 8 || meta == 0) {
                        decaySum    += percent * 10.0f;
                        totalWeight += 10;
                    }
                    decaySum    += percent;
                    totalWeight += 1;
                } else if (!world.registry.getById(id).isSolid) {
                    decaySum    += 1.0f;
                    totalWeight += 1;
                }
            }
        }

        if (totalWeight <= 0) return 0.8888889f;
        float cornerH = 1.0f - (decaySum / static_cast<float>(totalWeight));
        return std::clamp(cornerH, 0.05f, 1.0f);
    }

    static void emitFluidModel(const WorldMap& world, uint16_t st, int wx, int wy, int wz,
                               float lx, float ly, float lz, uint8_t skyL, uint8_t blkL,
                               std::vector<uint64_t>& dst) {
        uint8_t id = static_cast<uint8_t>((st >> 4) & 0xFF);
        bool isLava = (id == 10 || id == 11);
        bool isWater = !isLava;
        uint16_t spr = isLava ? SPR_LAVA_STILL : SPR_WATER_STILL;

        auto isSameFluid = [isLava](uint8_t nid) -> bool {
            return isLava ? (nid == 10 || nid == 11) : (nid == 8 || nid == 9);
        };

        // Suppresses boundary faces against identical fluids, solid blocks, and frozen ice
        auto cullsFluidFace = [&](int qx, int qy, int qz) -> bool {
            if (qy < 0 || qy >= CHUNK_HEIGHT) return false;
            uint16_t nSt = world.getBlockAt(qx, qy, qz);
            uint8_t nid  = static_cast<uint8_t>((nSt >> 4) & 0xFF);
            if (isSameFluid(nid)) return true;
            if (isWater && nid == 79) return true; // Culls water directly contacting ice
            return world.registry.getById(nid).isOpaque;
        };

        float h00 = getFluidCornerHeight(world, wx,     wy, wz,     isLava);
        float h10 = getFluidCornerHeight(world, wx + 1, wy, wz,     isLava);
        float h11 = getFluidCornerHeight(world, wx + 1, wy, wz + 1, isLava);
        float h01 = getFluidCornerHeight(world, wx,     wy, wz + 1, isLava);

        // Top surface manifold
        if (!isSameFluid(static_cast<uint8_t>((world.getBlockAt(wx, wy + 1, wz) >> 4) & 0xFF))) {
            float top[4][3] = {
                {lx,     ly + h01, lz + 1},
                {lx + 1, ly + h11, lz + 1},
                {lx + 1, ly + h10, lz},
                {lx,     ly + h00, lz}
            };
            emitQuad(dst, top, FACE_TOP, skyL, blkL, spr, TINT_NONE);
        }

        // Bottom substrate face
        if (!cullsFluidFace(wx, wy - 1, wz)) {
            float bot[4][3] = {{lx, ly, lz}, {lx+1, ly, lz}, {lx+1, ly, lz+1}, {lx, ly, lz+1}};
            emitQuad(dst, bot, FACE_BOTTOM, skyL, blkL, spr, TINT_NONE);
        }

        // Perimeter lateral quads
        if (!cullsFluidFace(wx + 1, wy, wz)) {
            float p[4][3] = {{lx+1, ly, lz+1}, {lx+1, ly, lz}, {lx+1, ly+h10, lz}, {lx+1, ly+h11, lz+1}};
            emitQuad(dst, p, FACE_EAST, skyL, blkL, spr, TINT_NONE);
        }
        if (!cullsFluidFace(wx - 1, wy, wz)) {
            float p[4][3] = {{lx, ly, lz}, {lx, ly, lz+1}, {lx, ly+h01, lz+1}, {lx, ly+h00, lz}};
            emitQuad(dst, p, FACE_WEST, skyL, blkL, spr, TINT_NONE);
        }
        if (!cullsFluidFace(wx, wy, wz + 1)) {
            float p[4][3] = {{lx, ly, lz+1}, {lx+1, ly, lz+1}, {lx+1, ly+h11, lz+1}, {lx, ly+h01, lz+1}};
            emitQuad(dst, p, FACE_SOUTH, skyL, blkL, spr, TINT_NONE);
        }
        if (!cullsFluidFace(wx, wy, wz - 1)) {
            float p[4][3] = {{lx+1, ly, lz}, {lx, ly, lz}, {lx, ly+h00, lz}, {lx+1, ly+h10, lz}};
            emitQuad(dst, p, FACE_NORTH, skyL, blkL, spr, TINT_NONE);
        }
    }

    // Dual-Axis Nether Portal & Thermal Atmospheric Flame
    static void emitPaneOrFireModel(uint16_t st, const BlockDef& def, float lx, float ly, float lz,
                                    uint8_t skyL, uint8_t blkL, std::vector<uint64_t>& dst) {
        uint16_t spr = def.faceTex[FACE_NORTH];
        if (def.shape == SHAPE_PORTAL) {
            uint8_t pMeta = static_cast<uint8_t>(st & 0x0F);
            if (pMeta == 2) {
                // Oriented along Z axis: spans Z, centered at X = 0.5
                float p1[4][3] = {{lx+0.375f, ly, lz+1}, {lx+0.375f, ly, lz}, {lx+0.375f, ly+1, lz}, {lx+0.375f, ly+1, lz+1}};
                float p2[4][3] = {{lx+0.625f, ly, lz}, {lx+0.625f, ly, lz+1}, {lx+0.625f, ly+1, lz+1}, {lx+0.625f, ly+1, lz}};
                emitQuad(dst, p1, FACE_EAST, skyL, blkL, spr, TINT_NONE);
                emitQuad(dst, p2, FACE_WEST, skyL, blkL, spr, TINT_NONE);
            } else {
                // Oriented along X axis: spans X, centered at Z = 0.5
                float p1[4][3] = {{lx, ly, lz+0.375f}, {lx+1, ly, lz+0.375f}, {lx+1, ly+1, lz+0.375f}, {lx, ly+1, lz+0.375f}};
                float p2[4][3] = {{lx+1, ly, lz+0.625f}, {lx, ly, lz+0.625f}, {lx, ly+1, lz+0.625f}, {lx+1, ly+1, lz+0.625f}};
                emitQuad(dst, p1, FACE_SOUTH, skyL, blkL, spr, TINT_NONE);
                emitQuad(dst, p2, FACE_NORTH, skyL, blkL, spr, TINT_NONE);
            }
        } else {
            // Crossed fire planes
            float p1[4][3] = {{lx, ly, lz}, {lx+1, ly, lz+1}, {lx+1, ly+1, lz+1}, {lx, ly+1, lz}};
            float p2[4][3] = {{lx, ly, lz+1}, {lx+1, ly, lz}, {lx+1, ly+1, lz}, {lx, ly+1, lz+1}};
            emitQuad(dst, p1, FACE_TOP, skyL, blkL, spr, TINT_NONE);
            emitQuad(dst, p2, FACE_TOP, skyL, blkL, spr, TINT_NONE);
        }
    }

    // Quantized Sub-Box with automatic solid neighbor face culling
    static void emitSubBox(const SubBox& box, float lx, float ly, float lz,
                           uint8_t skyL, uint8_t blkL, uint8_t extraData, std::vector<uint64_t>& dst,
                           const WorldMap* world = nullptr, int wx = 0, int wy = 0, int wz = 0) {
        float x0 = lx + box.minX * 0.0625f, y0 = ly + box.minY * 0.0625f, z0 = lz + box.minZ * 0.0625f;
        float x1 = lx + box.maxX * 0.0625f, y1 = ly + box.maxY * 0.0625f, z1 = lz + box.maxZ * 0.0625f;

        auto isNeighborOpaque = [&](int dx, int dy, int dz) -> bool {
            if (!world) return false;
            int qy = wy + dy;
            if (qy < 0 || qy >= CHUNK_HEIGHT) return false;
            return world->registry.get(world->getBlockAt(wx + dx, qy, wz + dz)).isOpaqueCube;
        };

        if (!(box.maxX >= 16.0f && isNeighborOpaque(1, 0, 0))) {
            float pE[4][3] = {{x1,y0,z1}, {x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}};
            emitQuad(dst, pE, FACE_EAST, skyL, blkL, box.faceTex[FACE_EAST], box.tint, extraData);
        }
        if (!(box.minX <= 0.0f && isNeighborOpaque(-1, 0, 0))) {
            float pW[4][3] = {{x0,y0,z0}, {x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}};
            emitQuad(dst, pW, FACE_WEST, skyL, blkL, box.faceTex[FACE_WEST], box.tint, extraData);
        }
        if (!(box.maxY >= 16.0f && isNeighborOpaque(0, 1, 0))) {
            float pT[4][3] = {{x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, {x0,y1,z0}};
            emitQuad(dst, pT, FACE_TOP, skyL, blkL, box.faceTex[FACE_TOP], box.tint, extraData);
        }
        if (!(box.minY <= 0.0f && isNeighborOpaque(0, -1, 0))) {
            float pB[4][3] = {{x0,y0,z0}, {x1,y0,z0}, {x1,y0,z1}, {x0,y0,z1}};
            emitQuad(dst, pB, FACE_BOTTOM, skyL, blkL, box.faceTex[FACE_BOTTOM], box.tint, extraData);
        }
        if (!(box.maxZ >= 16.0f && isNeighborOpaque(0, 0, 1))) {
            float pS[4][3] = {{x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}};
            emitQuad(dst, pS, FACE_SOUTH, skyL, blkL, box.faceTex[FACE_SOUTH], box.tint, extraData);
        }
        if (!(box.minZ <= 0.0f && isNeighborOpaque(0, 0, -1))) {
            float pN[4][3] = {{x1,y0,z0}, {x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}};
            emitQuad(dst, pN, FACE_NORTH, skyL, blkL, box.faceTex[FACE_NORTH], box.tint, extraData);
        }
    }
};

// Unified Geometry Pipeline Hub
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