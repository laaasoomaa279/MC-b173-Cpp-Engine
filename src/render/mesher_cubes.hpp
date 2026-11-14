#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "world/world.hpp"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

// ============================================================================
// Minecraft Beta 1.7.3 Cube Mesher (RenderBlocks.java 1:1 Fidelity)
// ============================================================================
// - Exact 1/16th block fixed-point coordinates [0..256]
// - Mojang RenderBlocks.renderStandardBlockWithAmbientOcclusion corner light
// - Authentic Beta 1.7.3 Fancy Grass Side Overlay (SPR_GRASS_SIDE_OVERLAY = 38)
// - Diagonal quad flipping based on AO to prevent interpolation seams
// ============================================================================

struct SectionMeshData {
    int32_t               chunkX = 0;
    int32_t               secY   = 0;
    int32_t               chunkZ = 0;
    std::vector<uint64_t> opaqueVertices;
    std::vector<uint64_t> translucentVertices;
};

inline uint64_t packChunkVertex(float lx, float ly, float lz,
                                uint8_t face, uint8_t ao, uint8_t skyL, uint8_t blkL,
                                uint16_t spriteIdx, uint8_t quadW, uint8_t quadH,
                                uint8_t tintMode, uint8_t extraData = 0, uint8_t uvCorner = 0) {
    uint32_t ix = (uint32_t)std::clamp((int)std::round(lx * 16.0f), 0, 256);
    uint32_t iy = (uint32_t)std::clamp((int)std::round(ly * 16.0f), 0, 256);
    uint32_t iz = (uint32_t)std::clamp((int)std::round(lz * 16.0f), 0, 256);

    uint32_t w0 = (ix & 0x1FFu) |
                  ((iy & 0x1FFu) << 9) |
                  ((iz & 0x1FFu) << 18) |
                  (((uint32_t)face & 0x07u) << 27) |
                  (((uint32_t)ao   & 0x03u) << 30);

    uint32_t w1 = ((uint32_t)spriteIdx & 0xFFu) |
                  (((uint32_t)std::clamp<int>(quadW, 1, 16) & 0x1Fu) << 8) |
                  (((uint32_t)std::clamp<int>(quadH, 1, 16) & 0x1Fu) << 13) |
                  (((uint32_t)tintMode & 0x03u) << 18) |
                  (((uint32_t)skyL     & 0x0Fu) << 20) |
                  (((uint32_t)blkL     & 0x0Fu) << 24) |
                  (((uint32_t)uvCorner & 0x03u) << 28) |
                  ((((uint32_t)extraData >> 2) & 0x03u) << 30);

    return ((uint64_t)w1 << 32) | (uint64_t)w0;
}

class BetaCubeMesher {
public:
    struct FaceCell {
        uint16_t state       = 0;
        uint16_t sprite      = 0;
        uint8_t  tint        = 0;
        uint8_t  ao[4]       = {3, 3, 3, 3};
        uint8_t  sky[4]      = {15, 15, 15, 15};
        uint8_t  blk[4]      = {0, 0, 0, 0};
        bool     translucent = false;
        bool     grassSide   = false; // Triggers Mojang Fancy Grass Side Overlay (sprite 38)

        bool operator==(const FaceCell& o) const {
            return state == o.state && sprite == o.sprite && tint == o.tint &&
                   translucent == o.translucent && grassSide == o.grassSide &&
                   ao[0] == o.ao[0] && ao[1] == o.ao[1] && ao[2] == o.ao[2] && ao[3] == o.ao[3] &&
                   sky[0] == o.sky[0] && sky[1] == o.sky[1] && sky[2] == o.sky[2] && sky[3] == o.sky[3] &&
                   blk[0] == o.blk[0] && blk[1] == o.blk[1] && blk[2] == o.blk[2] && blk[3] == o.blk[3];
        }
    };

    static void buildFullCubesGreedy(const WorldMap& world, int32_t cx, int32_t secY, int32_t cz,
                                     bool smoothLighting,
                                     std::vector<uint64_t>& outOpaque,
                                     std::vector<uint64_t>& outTranslucent) {
        const ChunkColumn* col = world.getReadyChunk(cx, cz);
        if (!col) return;
        const ChunkSection& sec = col->sections[secY];
        int baseX = cx * 16, baseY = secY * 16, baseZ = cz * 16;

        FaceCell mask[16 * 16];

        for (int face = 0; face < 6; ++face) {
            int axis  = face >> 1; // 0=X, 1=Y, 2=Z
            int dir   = (face & 1) ? -1 : 1;
            int uAxis = (axis == 0) ? 2 : 0;
            int vAxis = (axis == 1) ? 2 : 1;

            for (int slice = 0; slice < 16; ++slice) {
                std::memset(mask, 0, sizeof(mask));

                for (int v = 0; v < 16; ++v) {
                    for (int u = 0; u < 16; ++u) {
                        int pos[3];
                        pos[axis]  = slice;
                        pos[uAxis] = u;
                        pos[vAxis] = v;

                        uint16_t st = sec.getBlock(pos[0], pos[1], pos[2]);
                        if (st == 0) continue;

                        const BlockDef& def = world.registry.get(st);
                        if (def.shape != SHAPE_CUBE) continue;

                        int npos[3] = { pos[0], pos[1], pos[2] };
                        npos[axis] += dir;

                        int wx = baseX + pos[0], wy = baseY + pos[1], wz = baseZ + pos[2];
                        int nwx = baseX + npos[0], nwy = baseY + npos[1], nwz = baseZ + npos[2];

                        uint16_t nbSt = (npos[0] >= 0 && npos[0] < 16 && npos[1] >= 0 && npos[1] < 16 && npos[2] >= 0 && npos[2] < 16)
                                        ? sec.getBlock(npos[0], npos[1], npos[2])
                                        : world.getBlockAt(nwx, nwy, nwz);

                        const BlockDef& nbDef = world.registry.get(nbSt);
                        uint8_t blkId = (uint8_t)((st >> 4) & 0xFF);
                        bool isTrans = !def.isOpaque;

                        if (nbDef.isOpaque) continue;
                        if (isTrans && blkId != 18 && ((nbSt >> 4) & 0xFF) == blkId) continue;

                        FaceCell& cell = mask[v * 16 + u];
                        cell.state       = st;
                        cell.sprite      = world.registry.resolveFaceTexture(st, (BlockFace)face);
                        cell.tint        = (face == FACE_TOP || def.tintMode == TINT_FOLIAGE) ? def.tintMode : TINT_NONE;
                        cell.translucent = isTrans;
                        cell.grassSide   = (blkId == 2 && face != FACE_TOP && face != FACE_BOTTOM);

                        computeFaceLighting(world, wx, wy, wz, nwx, nwy, nwz, (BlockFace)face,
                                            smoothLighting, cell.ao, cell.sky, cell.blk);
                    }
                }

                for (int v = 0; v < 16; ++v) {
                    for (int u = 0; u < 16; ) {
                        FaceCell first = mask[v * 16 + u];
                        if (first.state == 0) { ++u; continue; }

                        int w = 1;
                        while (u + w < 16 && mask[v * 16 + u + w] == first) ++w;

                        int h = 1;
                        bool done = false;
                        while (v + h < 16 && !done) {
                            for (int k = 0; k < w; ++k) {
                                if (!(mask[(v + h) * 16 + u + k] == first)) { done = true; break; }
                            }
                            if (!done) ++h;
                        }

                        emitCubeFaceQuad(face, slice, u, v, w, h, first,
                                         first.translucent ? outTranslucent : outOpaque);

                        // Mojang Fancy Grass Side Overlay (renders sprite 38 tinted with grasscolor.png)
                        if (first.grassSide) {
                            FaceCell overlay = first;
                            overlay.sprite = SPR_GRASS_SIDE_OVERLAY;
                            overlay.tint   = TINT_GRASS;
                            emitCubeFaceQuad(face, slice, u, v, w, h, overlay, outTranslucent, 0.002f);
                        }

                        for (int dy = 0; dy < h; ++dy) {
                            for (int dx = 0; dx < w; ++dx) {
                                mask[(v + dy) * 16 + u + dx].state = 0;
                            }
                        }
                        u += w;
                    }
                }
            }
        }
    }

private:
    static inline bool isOccluding(const WorldMap& world, int wx, int wy, int wz) {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return false;
        return world.registry.get(world.getBlockAt(wx, wy, wz)).isOpaque;
    }

    static void computeFaceLighting(const WorldMap& world, int wx, int wy, int wz,
                                    int nwx, int nwy, int nwz, BlockFace face,
                                    bool smooth, uint8_t outAO[4], uint8_t outSky[4], uint8_t outBlk[4]) {
        (void)wx; (void)wy; (void)wz;
        uint8_t baseSky = world.getSkyLightAt(nwx, nwy, nwz);
        uint8_t baseBlk = world.getBlockLightAt(nwx, nwy, nwz);

        if (!smooth) {
            for (int i = 0; i < 4; ++i) {
                outAO[i]  = 3;
                outSky[i] = baseSky;
                outBlk[i] = baseBlk;
            }
            return;
        }

        int uVec[3]{}, vVec[3]{};
        int uSigns[4] = { -1,  1, 1, -1 };
        int vSigns[4] = { -1, -1, 1,  1 };

        if (face == FACE_EAST) {
            uVec[2] = -1; vVec[1] = 1;
        } else if (face == FACE_WEST) {
            uVec[2] = 1; vVec[1] = 1;
        } else if (face == FACE_TOP) {
            uVec[0] = 1; vVec[2] = -1;
        } else if (face == FACE_BOTTOM) {
            uVec[0] = 1; vVec[2] = 1;
        } else if (face == FACE_SOUTH) {
            uVec[0] = 1; vVec[1] = 1;
        } else {
            uVec[0] = -1; vVec[1] = 1;
        }

        for (int c = 0; c < 4; ++c) {
            int su = uSigns[c], sv = vSigns[c];
            int sx1 = nwx + uVec[0] * su, sy1 = nwy + uVec[1] * su, sz1 = nwz + uVec[2] * su;
            int sx2 = nwx + vVec[0] * sv, sy2 = nwy + vVec[1] * sv, sz2 = nwz + vVec[2] * sv;
            int cx  = nwx + uVec[0] * su + vVec[0] * sv;
            int cy  = nwy + uVec[1] * su + vVec[1] * sv;
            int cz  = nwz + uVec[2] * su + vVec[2] * sv;

            bool occ1 = isOccluding(world, sx1, sy1, sz1);
            bool occ2 = isOccluding(world, sx2, sy2, sz2);
            bool occC = isOccluding(world, cx,  cy,  cz);

            int ao = (occ1 && occ2) ? 0 : (3 - ((occ1 ? 1 : 0) + (occ2 ? 1 : 0) + (occC ? 1 : 0)));
            outAO[c] = (uint8_t)ao;

            int skySum = baseSky, blkSum = baseBlk, samples = 1;
            if (!occ1) { skySum += world.getSkyLightAt(sx1, sy1, sz1); blkSum += world.getBlockLightAt(sx1, sy1, sz1); samples++; }
            if (!occ2) { skySum += world.getSkyLightAt(sx2, sy2, sz2); blkSum += world.getBlockLightAt(sx2, sy2, sz2); samples++; }
            if ((!occ1 || !occ2) && !occC) {
                skySum += world.getSkyLightAt(cx, cy, cz);
                blkSum += world.getBlockLightAt(cx, cy, cz);
                samples++;
            }
            outSky[c] = (uint8_t)(skySum / samples);
            outBlk[c] = (uint8_t)(blkSum / samples);
        }
    }

    static void emitCubeFaceQuad(int face, int slice, int u, int v, int w, int h,
                                 const FaceCell& cell, std::vector<uint64_t>& dst, float nudge = 0.0f) {
        float p[4][3]{};
        if (face == FACE_EAST) {
            float x = (float)(slice + 1) + nudge;
            p[0][0]=x; p[0][1]=(float)v;     p[0][2]=(float)(u + w);
            p[1][0]=x; p[1][1]=(float)v;     p[1][2]=(float)u;
            p[2][0]=x; p[2][1]=(float)(v+h); p[2][2]=(float)u;
            p[3][0]=x; p[3][1]=(float)(v+h); p[3][2]=(float)(u + w);
        } else if (face == FACE_WEST) {
            float x = (float)slice - nudge;
            p[0][0]=x; p[0][1]=(float)v;     p[0][2]=(float)u;
            p[1][0]=x; p[1][1]=(float)v;     p[1][2]=(float)(u + w);
            p[2][0]=x; p[2][1]=(float)(v+h); p[2][2]=(float)(u + w);
            p[3][0]=x; p[3][1]=(float)(v+h); p[3][2]=(float)u;
        } else if (face == FACE_TOP) {
            float y = (float)(slice + 1) + nudge;
            p[0][0]=(float)u;     p[0][1]=y; p[0][2]=(float)(v + h);
            p[1][0]=(float)(u+w); p[1][1]=y; p[1][2]=(float)(v + h);
            p[2][0]=(float)(u+w); p[2][1]=y; p[2][2]=(float)v;
            p[3][0]=(float)u;     p[3][1]=y; p[3][2]=(float)v;
        } else if (face == FACE_BOTTOM) {
            float y = (float)slice - nudge;
            p[0][0]=(float)u;     p[0][1]=y; p[0][2]=(float)v;
            p[1][0]=(float)(u+w); p[1][1]=y; p[1][2]=(float)v;
            p[2][0]=(float)(u+w); p[2][1]=y; p[2][2]=(float)(v + h);
            p[3][0]=(float)u;     p[3][1]=y; p[3][2]=(float)(v + h);
        } else if (face == FACE_SOUTH) {
            float z = (float)(slice + 1) + nudge;
            p[0][0]=(float)u;     p[0][1]=(float)v;     p[0][2]=z;
            p[1][0]=(float)(u+w); p[1][1]=(float)v;     p[1][2]=z;
            p[2][0]=(float)(u+w); p[2][1]=(float)(v+h); p[2][2]=z;
            p[3][0]=(float)u;     p[3][1]=(float)(v+h); p[3][2]=z;
        } else {
            float z = (float)slice - nudge;
            p[0][0]=(float)(u+w); p[0][1]=(float)v;     p[0][2]=z;
            p[1][0]=(float)u;     p[1][1]=(float)v;     p[1][2]=z;
            p[2][0]=(float)u;     p[2][1]=(float)(v+h); p[2][2]=z;
            p[3][0]=(float)(u+w); p[3][1]=(float)(v+h); p[3][2]=z;
        }

        uint64_t verts[4];
        for (uint8_t c = 0; c < 4; ++c) {
            verts[c] = packChunkVertex(p[c][0], p[c][1], p[c][2],
                                       (uint8_t)face, cell.ao[c], cell.sky[c], cell.blk[c],
                                       cell.sprite, (uint8_t)w, (uint8_t)h,
                                       cell.tint, 0, c);
        }

        if (cell.ao[0] + cell.ao[2] >= cell.ao[1] + cell.ao[3]) {
            dst.push_back(verts[0]); dst.push_back(verts[1]); dst.push_back(verts[2]);
            dst.push_back(verts[0]); dst.push_back(verts[2]); dst.push_back(verts[3]);
        } else {
            dst.push_back(verts[1]); dst.push_back(verts[2]); dst.push_back(verts[3]);
            dst.push_back(verts[1]); dst.push_back(verts[3]); dst.push_back(verts[0]);
        }
    }
};