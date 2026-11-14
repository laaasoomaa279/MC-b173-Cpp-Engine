#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "world/world.hpp"
#include "world/noise_b173.hpp"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

// ============================================================================
// CANONICAL BIOSPHERIC FEATURE GENERATORS (SPEC-2011)
// ============================================================================
// Technical Subsystem Architecture:
// 1. Hydrothermal Basin Synthesis (Lakes & Subterranean Lava Pockets):
//    - Carves intersecting ellipsoidal geometries (4 to 8 stochastic centroids)
//      with strict peripheral boundary checks preventing ocean basin puncturing.
// 2. Subterranean Crypt Architecture (Dungeons):
//    - Dimensions: 5x5 to 7x7 perimeter walls (Cobblestone / Moss Stone 75% bias).
//    - Structural Wall Validation: Ensures exactly 1 to 5 external air breaches.
//    - Loot Generation & Monster Spawner installation with canonical weights.
// 3. Dendrological Architecture & Golden-Ratio Multi-Branch Trees:
//    - Standard Arboreal Forms: Oak (meta 0), Birch (meta 2), Taiga Spruce (meta 1).
//    - Big Oak Procedural Generation:
//        * Height partitioning: trunk_height = floor(height_limit * 0.618).
//        * Node dispersion: branch layers generated along Y with lateral offsets.
//        * Spherical crown foliage envelopes: radius = sqrt(R^2 - dy^2) * 0.5.
// 4. Riparian & Xerophytic Ground Cover (Fixed Invariants):
//    - Reeds (83): Ground level MUST be Dirt/Grass/Sand and immediately adjacent
//      to water on horizontal cardinal offsets. Upward stacking checks canBlockStay
//      per segment, completely eliminating floating disjointed stalks.
//    - Cactus (81): Exclusively founded on Sand (12), verifying 4-way horizontal
//      clearance to prevent perimeter contact with any solid blocks.
//    - Cryospheric Pass: Snow layers (78) strictly verify canSupportSnow() on
//      the substrate block, preventing invalid snow placement over non-full blocks
//      (such as Cacti, Slabs, Stairs, and Ice).
// ============================================================================

class WorldFeatures {
public:
    static inline uint16_t getBlockSafe(const WorldMap& world, int wx, int wy, int wz) {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return 0;
        const ChunkColumn* c = world.getTerrainOrReadyChunk(wx >> 4, wz >> 4);
        if (!c) return 0;
        return c->sections[wy >> 4].getBlock(wx & 15, wy & 15, wz & 15);
    }

    static inline void setBlockSafe(WorldMap& world, int wx, int wy, int wz, uint16_t st, bool onlyAir = false) {
        if (wy < 1 || wy >= CHUNK_HEIGHT - 1) return;
        int32_t tcx = wx >> 4, tcz = wz >> 4;
        std::lock_guard<std::mutex> lock(world.getChunkLock(tcx, tcz));
        ChunkColumn* target = world.getTerrainOrReadyChunk(tcx, tcz);
        if (!target) return;
        int lx = wx & 15, lz = wz & 15, sy = wy >> 4;
        if (onlyAir && target->sections[sy].getBlock(lx, wy & 15, lz) != 0) return;
        target->sections[sy].setBlock(lx, wy & 15, lz, st);
    }

    static inline int getSurfaceSolidY(const WorldMap& world, int wx, int wz) {
        for (int y = CHUNK_HEIGHT - 1; y > 0; --y) {
            uint8_t id = static_cast<uint8_t>((getBlockSafe(world, wx, y, wz) >> 4) & 0xFF);
            if (id != 0 && id != 18 && id != 78 && id != 31 && id != 37 && id != 38) {
                return y + 1;
            }
        }
        return 0;
    }

    // 1. Water & Lava Lakes
    static void generateWorldLake(WorldMap& world, JavaRandomB173& rand, int wx, int wy, int wz,
                                  uint16_t liquidState, bool lakeGrid[16 * 16 * 8]) {
        wx -= 8;
        wz -= 8;
        while (wy > 5 && getBlockSafe(world, wx, wy, wz) == 0) {
            wy--;
        }
        if (wy <= 4) return;
        wy -= 4;

        std::memset(lakeGrid, 0, 16 * 16 * 8 * sizeof(bool));
        int blobs = rand.nextInt(4) + 4;
        for (int b = 0; b < blobs; ++b) {
            double rx = rand.nextDouble() * 6.0 + 3.0;
            double ry = rand.nextDouble() * 4.0 + 2.0;
            double rz = rand.nextDouble() * 6.0 + 3.0;
            double cx = rand.nextDouble() * (16.0 - rx - 2.0) + 1.0 + rx / 2.0;
            double cy = rand.nextDouble() * (8.0  - ry - 4.0) + 2.0 + ry / 2.0;
            double cz = rand.nextDouble() * (16.0 - rz - 2.0) + 1.0 + rz / 2.0;
            for (int x = 1; x < 15; ++x) {
                for (int z = 1; z < 15; ++z) {
                    for (int y = 1; y < 7; ++y) {
                        double dx = (static_cast<double>(x) - cx) / (rx / 2.0);
                        double dy = (static_cast<double>(y) - cy) / (ry / 2.0);
                        double dz = (static_cast<double>(z) - cz) / (rz / 2.0);
                        if (dx * dx + dy * dy + dz * dz < 1.0) {
                            lakeGrid[(x * 16 + z) * 8 + y] = true;
                        }
                    }
                }
            }
        }

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < 8; ++y) {
                    bool flag = !lakeGrid[(x * 16 + z) * 8 + y] &&
                        ((x < 15 && lakeGrid[((x + 1) * 16 + z) * 8 + y]) ||
                         (x > 0  && lakeGrid[((x - 1) * 16 + z) * 8 + y]) ||
                         (z < 15 && lakeGrid[(x * 16 + (z + 1)) * 8 + y]) ||
                         (z > 0  && lakeGrid[(x * 16 + (z - 1)) * 8 + y]) ||
                         (y < 7  && lakeGrid[(x * 16 + z) * 8 + (y + 1)]) ||
                         (y > 0  && lakeGrid[(x * 16 + z) * 8 + (y - 1)]));
                    if (!flag) continue;
                    uint8_t id = static_cast<uint8_t>((getBlockSafe(world, wx + x, wy + y, wz + z) >> 4) & 0xFF);
                    if (y >= 4 && (id == 8 || id == 9 || id == 10 || id == 11)) return;
                    if (y < 4 && !world.registry.getById(id).isSolid && getBlockSafe(world, wx + x, wy + y, wz + z) != liquidState) return;
                }
            }
        }

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < 8; ++y) {
                    if (!lakeGrid[(x * 16 + z) * 8 + y]) continue;
                    int tx = wx + x, ty = wy + y, tz = wz + z;
                    setBlockSafe(world, tx, ty, tz, (y >= 4) ? 0 : liquidState);
                }
            }
        }
    }

    // 2. Dungeons
    static void generateDungeon(WorldMap& world, JavaRandomB173& rand, int wx, int wy, int wz) {
        int rx = rand.nextInt(2) + 2;
        int rz = rand.nextInt(2) + 2;
        int openings = 0;

        for (int dx = -rx - 1; dx <= rx + 1; ++dx) {
            for (int dy = -1; dy <= 4; ++dy) {
                for (int dz = -rz - 1; dz <= rz + 1; ++dz) {
                    uint8_t id = static_cast<uint8_t>((getBlockSafe(world, wx + dx, wy + dy, wz + dz) >> 4) & 0xFF);
                    bool solid = world.registry.getById(id).isSolid;
                    if (dy == -1 && !solid) return;
                    if (dy == 4  && !solid) return;
                    if ((dx == -rx - 1 || dx == rx + 1 || dz == -rz - 1 || dz == rz + 1) && dy == 0 && id == 0 && getBlockSafe(world, wx + dx, wy + dy + 1, wz + dz) == 0) {
                        openings++;
                    }
                }
            }
        }

        if (openings < 1 || openings > 5) return;

        for (int dx = -rx - 1; dx <= rx + 1; ++dx) {
            for (int dy = 3; dy >= -1; --dy) {
                for (int dz = -rz - 1; dz <= rz + 1; ++dz) {
                    if (dx == -rx - 1 || dy == -1 || dz == -rz - 1 || dx == rx + 1 || dy == 4 || dz == rz + 1) {
                        if (wy + dy >= 0 && !world.registry.get(getBlockSafe(world, wx + dx, wy + dy - 1, wz + dz)).isSolid) {
                            setBlockSafe(world, wx + dx, wy + dy, wz + dz, 0);
                        } else if (world.registry.get(getBlockSafe(world, wx + dx, wy + dy, wz + dz)).isSolid) {
                            if (dy == -1 && rand.nextInt(4) != 0) setBlockSafe(world, wx + dx, wy + dy, wz + dz, (48 << 4));
                            else                                   setBlockSafe(world, wx + dx, wy + dy, wz + dz, (4 << 4));
                        }
                    } else {
                        setBlockSafe(world, wx + dx, wy + dy, wz + dz, 0);
                    }
                }
            }
        }

        // Up to 2 Dungeon Chests
        for (int chestAttempt = 0; chestAttempt < 2; ++chestAttempt) {
            for (int tryIdx = 0; tryIdx < 3; ++tryIdx) {
                int cxP = wx + rand.nextInt(rx * 2 + 1) - rx;
                int czP = wz + rand.nextInt(rz * 2 + 1) - rz;
                if (getBlockSafe(world, cxP, wy, czP) != 0) continue;

                int adjWalls = 0;
                if (world.registry.get(getBlockSafe(world, cxP - 1, wy, czP)).isSolid) adjWalls++;
                if (world.registry.get(getBlockSafe(world, cxP + 1, wy, czP)).isSolid) adjWalls++;
                if (world.registry.get(getBlockSafe(world, cxP, wy, czP - 1)).isSolid) adjWalls++;
                if (world.registry.get(getBlockSafe(world, cxP, wy, czP + 1)).isSolid) adjWalls++;
                if (adjWalls != 1) continue;

                setBlockSafe(world, cxP, wy, czP, (54 << 4));
                TileEntityData te;
                te.id = "Chest";
                te.x = cxP; te.y = wy; te.z = czP;

                for (int itemRoll = 0; itemRoll < 8; ++itemRoll) {
                    ItemStack loot{};
                    int pick = rand.nextInt(11);
                    if (pick == 0)      loot = { ITEM_SADDLE, 0, 1 };
                    else if (pick == 1) loot = { ITEM_IRON_INGOT, 0, static_cast<uint8_t>(rand.nextInt(4) + 1) };
                    else if (pick == 2) loot = { ITEM_BREAD, 0, 1 };
                    else if (pick == 3) loot = { ITEM_WHEAT, 0, static_cast<uint8_t>(rand.nextInt(4) + 1) };
                    else if (pick == 4) loot = { ITEM_GUNPOWDER, 0, static_cast<uint8_t>(rand.nextInt(4) + 1) };
                    else if (pick == 5) loot = { ITEM_STRING, 0, static_cast<uint8_t>(rand.nextInt(4) + 1) };
                    else if (pick == 6) loot = { ITEM_BUCKET, 0, 1 };
                    else if (pick == 7 && rand.nextInt(100) == 0) loot = { ITEM_GOLDEN_APPLE, 0, 1 };
                    else if (pick == 8 && rand.nextInt(2) == 0)   loot = { ITEM_REDSTONE, 0, static_cast<uint8_t>(rand.nextInt(4) + 1) };
                    else if (pick == 9 && rand.nextInt(10) == 0)  loot = { static_cast<uint16_t>(ITEM_RECORD_13 + rand.nextInt(2)), 0, 1 };
                    else if (pick == 10) loot = { ITEM_DYE, 3, 1 };

                    if (!loot.isEmpty()) {
                        te.slots[rand.nextInt(27)] = loot;
                    }
                }

                std::lock_guard<std::mutex> teLock(world.tileEntityMutex);
                world.tileEntities[{ cxP, wy, czP }] = std::move(te);
                break;
            }
        }

        // Center Spawner
        setBlockSafe(world, wx, wy, wz, (52 << 4));
    }

    // 3. Clay Disks
    static void generateClayPatch(WorldMap& world, JavaRandomB173& rand, int wx, int wy, int wz, int numberOfBlocks) {
        uint8_t startId = static_cast<uint8_t>((getBlockSafe(world, wx, wy, wz) >> 4) & 0xFF);
        if (startId != 8 && startId != 9) return;

        float angle = rand.nextFloat() * 3.1415927f;
        double x1 = static_cast<double>(wx + 8) + std::sin(angle) * static_cast<double>(numberOfBlocks) / 8.0;
        double x2 = static_cast<double>(wx + 8) - std::sin(angle) * static_cast<double>(numberOfBlocks) / 8.0;
        double z1 = static_cast<double>(wz + 8) + std::cos(angle) * static_cast<double>(numberOfBlocks) / 8.0;
        double z2 = static_cast<double>(wz + 8) - std::cos(angle) * static_cast<double>(numberOfBlocks) / 8.0;
        double y1 = static_cast<double>(wy + rand.nextInt(3) + 2);
        double y2 = static_cast<double>(wy + rand.nextInt(3) + 2);

        for (int step = 0; step <= numberOfBlocks; ++step) {
            double frac = static_cast<double>(step) / static_cast<double>(numberOfBlocks);
            double cxP  = x1 + (x2 - x1) * frac;
            double cyP  = y1 + (y2 - y1) * frac;
            double czP  = z1 + (z2 - z1) * frac;
            double spread = rand.nextDouble() * static_cast<double>(numberOfBlocks) / 16.0;
            double radH = (std::sin(static_cast<float>(frac) * 3.1415927f) + 1.0) * spread + 1.0;

            int minX = static_cast<int>(std::floor(cxP - radH / 2.0));
            int minY = static_cast<int>(std::floor(cyP - radH / 2.0));
            int minZ = static_cast<int>(std::floor(czP - radH / 2.0));
            int maxX = static_cast<int>(std::floor(cxP + radH / 2.0));
            int maxY = static_cast<int>(std::floor(cyP + radH / 2.0));
            int maxZ = static_cast<int>(std::floor(czP + radH / 2.0));

            for (int vx = minX; vx <= maxX; ++vx) {
                for (int vy = minY; vy <= maxY; ++vy) {
                    if (vy < 1 || vy >= CHUNK_HEIGHT - 1) continue;
                    for (int vz = minZ; vz <= maxZ; ++vz) {
                        double dx = (static_cast<double>(vx) + 0.5 - cxP) / (radH / 2.0);
                        double dy = (static_cast<double>(vy) + 0.5 - cyP) / (radH / 2.0);
                        double dz = (static_cast<double>(vz) + 0.5 - czP) / (radH / 2.0);
                        if (dx * dx + dy * dy + dz * dz < 1.0 && ((getBlockSafe(world, vx, vy, vz) >> 4) & 0xFF) == 12) {
                            setBlockSafe(world, vx, vy, vz, (82 << 4));
                        }
                    }
                }
            }
        }
    }

    // 4. Ore Veins
    static void generateVein(WorldMap& world, JavaRandomB173& rand, int chunkBaseX, int chunkBaseZ,
                             uint16_t oreState, int size, int attempts, int minY, int maxY) {
        for (int a = 0; a < attempts; ++a) {
            int bx = chunkBaseX + rand.nextInt(16);
            int by = minY + rand.nextInt(std::max(1, maxY - minY));
            int bz = chunkBaseZ + rand.nextInt(16);

            float angle = rand.nextFloat() * 3.1415927f;
            double x1 = static_cast<double>(bx + 8) + std::sin(angle) * static_cast<double>(size) / 8.0;
            double x2 = static_cast<double>(bx + 8) - std::sin(angle) * static_cast<double>(size) / 8.0;
            double z1 = static_cast<double>(bz + 8) + std::cos(angle) * static_cast<double>(size) / 8.0;
            double z2 = static_cast<double>(bz + 8) - std::cos(angle) * static_cast<double>(size) / 8.0;
            double y1 = static_cast<double>(by + rand.nextInt(3) + 2);
            double y2 = static_cast<double>(by + rand.nextInt(3) + 2);

            for (int step = 0; step <= size; ++step) {
                double frac = static_cast<double>(step) / static_cast<double>(size);
                double cxP  = x1 + (x2 - x1) * frac;
                double cyP  = y1 + (y2 - y1) * frac;
                double czP  = z1 + (z2 - z1) * frac;
                double spread = rand.nextDouble() * static_cast<double>(size) / 16.0;
                double radH = (std::sin(static_cast<float>(frac) * 3.1415927f) + 1.0) * spread + 1.0;
                double radV = radH;

                int minX = static_cast<int>(std::floor(cxP - radH / 2.0));
                int minYBox = static_cast<int>(std::floor(cyP - radV / 2.0));
                int minZ = static_cast<int>(std::floor(czP - radH / 2.0));
                int maxX = static_cast<int>(std::floor(cxP + radH / 2.0));
                int maxYBox = static_cast<int>(std::floor(cyP + radV / 2.0));
                int maxZ = static_cast<int>(std::floor(czP + radH / 2.0));

                for (int vx = minX; vx <= maxX; ++vx) {
                    double dx = (static_cast<double>(vx) + 0.5 - cxP) / (radH / 2.0);
                    if (dx * dx >= 1.0) continue;
                    for (int vy = minYBox; vy <= maxYBox; ++vy) {
                        if (vy < 1 || vy >= CHUNK_HEIGHT - 1) continue;
                        double dy = (static_cast<double>(vy) + 0.5 - cyP) / (radV / 2.0);
                        if (dx * dx + dy * dy >= 1.0) continue;
                        for (int vz = minZ; vz <= maxZ; ++vz) {
                            double dz = (static_cast<double>(vz) + 0.5 - czP) / (radH / 2.0);
                            if (dx * dx + dy * dy + dz * dz < 1.0) {
                                if (((getBlockSafe(world, vx, vy, vz) >> 4) & 0xFF) == 1) {
                                    setBlockSafe(world, vx, vy, vz, oreState, false);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 5. Trees
    static void generateStandardTree(WorldMap& world, JavaRandomB173& rand, int x, int y, int z, uint8_t woodMeta) {
        int h = rand.nextInt(3) + ((woodMeta == 2) ? 5 : 4);
        if (y < 1 || y + h + 1 > 128) return;

        for (int py = y; py <= y + 1 + h; ++py) {
            int rad = (py == y) ? 0 : ((py >= y + 1 + h - 2) ? 2 : 1);
            for (int px = x - rad; px <= x + rad; ++px) {
                for (int pz = z - rad; pz <= z + rad; ++pz) {
                    if (py < 0 || py >= 128) return;
                    uint8_t id = static_cast<uint8_t>((getBlockSafe(world, px, py, pz) >> 4) & 0xFF);
                    if (id != 0 && id != 18) return;
                }
            }
        }

        uint8_t ground = static_cast<uint8_t>((getBlockSafe(world, x, y - 1, z) >> 4) & 0xFF);
        if ((ground != 2 && ground != 3) || y >= 128 - h - 1) return;

        setBlockSafe(world, x, y - 1, z, (3 << 4), false);
        uint16_t logSt  = static_cast<uint16_t>((17 << 4) | woodMeta);
        uint16_t leafSt = static_cast<uint16_t>((18 << 4) | woodMeta);

        for (int py = y - 3 + h; py <= y + h; ++py) {
            int dy = py - (y + h);
            int rad = 1 - dy / 2;
            for (int px = x - rad; px <= x + rad; ++px) {
                int dx = px - x;
                for (int pz = z - rad; pz <= z + rad; ++pz) {
                    int dz = pz - z;
                    if (std::abs(dx) == rad && std::abs(dz) == rad && (rand.nextInt(2) == 0 || dy == 0)) continue;
                    uint8_t cur = static_cast<uint8_t>((getBlockSafe(world, px, py, pz) >> 4) & 0xFF);
                    if (cur == 0 || cur == 18) setBlockSafe(world, px, py, pz, leafSt, false);
                }
            }
        }
        for (int dy = 0; dy < h; ++dy) {
            uint8_t id = static_cast<uint8_t>((getBlockSafe(world, x, y + dy, z) >> 4) & 0xFF);
            if (id == 0 || id == 18) setBlockSafe(world, x, y + dy, z, logSt, false);
        }
    }

    static void generateTaigaTree1(WorldMap& world, JavaRandomB173& rand, int x, int y, int z) {
        int h = rand.nextInt(5) + 7;
        int trunkClear = h - rand.nextInt(2) - 3;
        int maxRad = 1 + rand.nextInt(h - trunkClear + 1);
        if (y < 1 || y + h + 1 > 128) return;

        uint8_t ground = static_cast<uint8_t>((getBlockSafe(world, x, y - 1, z) >> 4) & 0xFF);
        if ((ground != 2 && ground != 3) || y >= 128 - h - 1) return;
        setBlockSafe(world, x, y - 1, z, (3 << 4), false);

        int rad = 0;
        for (int py = y + h; py >= y + trunkClear; --py) {
            for (int px = x - rad; px <= x + rad; ++px) {
                int dx = px - x;
                for (int pz = z - rad; pz <= z + rad; ++pz) {
                    int dz = pz - z;
                    if ((std::abs(dx) != rad || std::abs(dz) != rad || rad <= 0) && getBlockSafe(world, px, py, pz) == 0) {
                        setBlockSafe(world, px, py, pz, (18 << 4) | 1, false);
                    }
                }
            }
            if (rad >= 1 && py == y + trunkClear + 1) rad--;
            else if (rad < maxRad) rad++;
        }
        for (int dy = 0; dy < h - 1; ++dy) {
            uint8_t id = static_cast<uint8_t>((getBlockSafe(world, x, y + dy, z) >> 4) & 0xFF);
            if (id == 0 || id == 18) setBlockSafe(world, x, y + dy, z, (17 << 4) | 1, false);
        }
    }

    static void generateTaigaTree2(WorldMap& world, JavaRandomB173& rand, int x, int y, int z) {
        int h = rand.nextInt(4) + 6;
        int trunkClear = 1 + rand.nextInt(2);
        int maxRad = 2 + rand.nextInt(2);
        if (y < 1 || y + h + 1 > 128) return;

        uint8_t ground = static_cast<uint8_t>((getBlockSafe(world, x, y - 1, z) >> 4) & 0xFF);
        if ((ground != 2 && ground != 3) || y >= 128 - h - 1) return;
        setBlockSafe(world, x, y - 1, z, (3 << 4), false);

        int rad = rand.nextInt(2);
        int stepTarget = 1;
        bool resetDone = false;
        for (int step = 0; step <= h - trunkClear; ++step) {
            int py = y + h - step;
            for (int px = x - rad; px <= x + rad; ++px) {
                int dx = px - x;
                for (int pz = z - rad; pz <= z + rad; ++pz) {
                    int dz = pz - z;
                    if ((std::abs(dx) != rad || std::abs(dz) != rad || rad <= 0) && getBlockSafe(world, px, py, pz) == 0) {
                        setBlockSafe(world, px, py, pz, (18 << 4) | 1, false);
                    }
                }
            }
            if (rad >= stepTarget) {
                rad = resetDone ? 1 : 0;
                resetDone = true;
                if (++stepTarget > maxRad) stepTarget = maxRad;
            } else {
                rad++;
            }
        }
        int topTrim = rand.nextInt(3);
        for (int dy = 0; dy < h - topTrim; ++dy) {
            uint8_t id = static_cast<uint8_t>((getBlockSafe(world, x, y + dy, z) >> 4) & 0xFF);
            if (id == 0 || id == 18) setBlockSafe(world, x, y + dy, z, (17 << 4) | 1, false);
        }
    }

    static void generateBigOakTree(WorldMap& world, JavaRandomB173& rand, int x, int y, int z) {
        JavaRandomB173 treeRand(rand.nextLong());
        int heightLimit = 5 + treeRand.nextInt(12);
        if (y < 1 || y + heightLimit + 1 > 128) return;

        uint8_t ground = static_cast<uint8_t>((getBlockSafe(world, x, y - 1, z) >> 4) & 0xFF);
        if (ground != 2 && ground != 3) return;

        int height = static_cast<int>(static_cast<double>(heightLimit) * 0.618);
        if (height >= heightLimit) height = heightLimit - 1;

        int clustersPerY = static_cast<int>(1.382 + std::pow(static_cast<double>(heightLimit) / 13.0, 2.0));
        if (clustersPerY < 1) clustersPerY = 1;

        struct BranchNode { int bx, by, bz, trunkY; };
        std::vector<BranchNode> nodes;
        nodes.reserve(64);

        int topY = y + heightLimit - 5;
        int curY = y + height;
        nodes.push_back({ x, topY, z, curY });
        topY--;

        while (topY >= y) {
            int relY = topY - y;
            float layerSize = -1.0f;
            if (static_cast<double>(relY) >= static_cast<double>(heightLimit) * 0.3) {
                float halfH = static_cast<float>(heightLimit) / 2.0f;
                float dist  = halfH - static_cast<float>(relY);
                if (dist == 0.0f) layerSize = halfH;
                else if (std::abs(dist) >= halfH) layerSize = 0.0f;
                else layerSize = static_cast<float>(std::sqrt(std::pow(halfH, 2.0) - std::pow(dist, 2.0)));
                layerSize *= 0.5f;
            }

            if (layerSize >= 0.0f) {
                for (int c = 0; c < clustersPerY; ++c) {
                    double r   = static_cast<double>(layerSize) * (static_cast<double>(treeRand.nextFloat()) + 0.328);
                    double ang = static_cast<double>(treeRand.nextFloat()) * 2.0 * 3.14159;
                    int bx = static_cast<int>(std::floor(r * std::sin(ang) + static_cast<double>(x) + 0.5));
                    int bz = static_cast<int>(std::floor(r * std::cos(ang) + static_cast<double>(z) + 0.5));
                    int by = topY;

                    double horizDist = std::sqrt(std::pow(std::abs(x - bx), 2.0) + std::pow(std::abs(z - bz), 2.0));
                    double slope = horizDist * 0.381;
                    int trunkY = (static_cast<double>(by) - slope > static_cast<double>(curY))
                                 ? curY
                                 : static_cast<int>(static_cast<double>(by) - slope);
                    nodes.push_back({ bx, by, bz, trunkY });
                }
            }
            topY--;
        }

        for (const auto& n : nodes) {
            for (int ly = n.by; ly < n.by + 5; ++ly) {
                int rel = ly - n.by;
                float rad = (rel >= 0 && rel < 5) ? ((rel == 0 || rel == 4) ? 2.0f : 3.0f) : -1.0f;
                int irad = static_cast<int>(rad + 0.618f);
                for (int dx = -irad; dx <= irad; ++dx) {
                    for (int dz = -irad; dz <= irad; ++dz) {
                        double d = std::pow(static_cast<double>(std::abs(dx)) + 0.5, 2.0) +
                                   std::pow(static_cast<double>(std::abs(dz)) + 0.5, 2.0);
                        if (d <= static_cast<double>(rad * rad)) {
                            uint8_t cur = static_cast<uint8_t>((getBlockSafe(world, n.bx + dx, ly, n.bz + dz) >> 4) & 0xFF);
                            if (cur == 0 || cur == 18) setBlockSafe(world, n.bx + dx, ly, n.bz + dz, (18 << 4), false);
                        }
                    }
                }
            }
        }

        setBlockSafe(world, x, y - 1, z, (3 << 4), false);
        for (int ty = y; ty < y + height; ++ty) {
            setBlockSafe(world, x, ty, z, (17 << 4), false);
        }

        for (const auto& n : nodes) {
            if (static_cast<double>(n.trunkY - y) < static_cast<double>(heightLimit) * 0.2) continue;
            int dx = n.bx - x, dy = n.by - n.trunkY, dz = n.bz - z;
            int steps = std::max({ std::abs(dx), std::abs(dy), std::abs(dz) });
            if (steps == 0) continue;
            for (int s = 0; s <= steps; ++s) {
                int lx = static_cast<int>(std::floor(static_cast<double>(x) + static_cast<double>(dx) * s / steps + 0.5));
                int ly = static_cast<int>(std::floor(static_cast<double>(n.trunkY) + static_cast<double>(dy) * s / steps + 0.5));
                int lz = static_cast<int>(std::floor(static_cast<double>(z) + static_cast<double>(dz) * s / steps + 0.5));
                setBlockSafe(world, lx, ly, lz, (17 << 4), false);
            }
        }
    }

    // 6. Flora & Ground Features
    static void generateFlowerPatch(WorldMap& world, JavaRandomB173& rand, int x, int y, int z, uint16_t flowerState) {
        for (int i = 0; i < 64; ++i) {
            int fx = x + rand.nextInt(8) - rand.nextInt(8);
            int fy = y + rand.nextInt(4) - rand.nextInt(4);
            int fz = z + rand.nextInt(8) - rand.nextInt(8);
            if (getBlockSafe(world, fx, fy, fz) == 0 && ((getBlockSafe(world, fx, fy - 1, fz) >> 4) & 0xFF) == 2) {
                setBlockSafe(world, fx, fy, fz, flowerState, true);
            }
        }
    }

    static void generateTallGrassPatch(WorldMap& world, JavaRandomB173& rand, int x, int y, int z, uint16_t grassState) {
        while (y > 0 && (getBlockSafe(world, x, y, z) == 0 || ((getBlockSafe(world, x, y, z) >> 4) & 0xFF) == 18)) y--;
        for (int i = 0; i < 128; ++i) {
            int gx = x + rand.nextInt(8) - rand.nextInt(8);
            int gy = y + rand.nextInt(4) - rand.nextInt(4);
            int gz = z + rand.nextInt(8) - rand.nextInt(8);
            uint8_t ground = static_cast<uint8_t>((getBlockSafe(world, gx, gy - 1, gz) >> 4) & 0xFF);
            if (getBlockSafe(world, gx, gy, gz) == 0 && (ground == 2 || ground == 3)) {
                setBlockSafe(world, gx, gy, gz, grassState, true);
            }
        }
    }

    static void generateDeadBushPatch(WorldMap& world, JavaRandomB173& rand, int x, int y, int z) {
        while (y > 0 && (getBlockSafe(world, x, y, z) == 0 || ((getBlockSafe(world, x, y, z) >> 4) & 0xFF) == 18)) y--;
        for (int i = 0; i < 4; ++i) {
            int dx = x + rand.nextInt(8) - rand.nextInt(8);
            int dy = y + rand.nextInt(4) - rand.nextInt(4);
            int dz = z + rand.nextInt(8) - rand.nextInt(8);
            if (getBlockSafe(world, dx, dy, dz) == 0 && ((getBlockSafe(world, dx, dy - 1, dz) >> 4) & 0xFF) == 12) {
                setBlockSafe(world, dx, dy, dz, (32 << 4), true);
            }
        }
    }

    static void generateMushroomPatch(WorldMap& world, JavaRandomB173& rand, int x, int y, int z, uint16_t mushState) {
        for (int i = 0; i < 64; ++i) {
            int mx = x + rand.nextInt(8) - rand.nextInt(8);
            int my = y + rand.nextInt(4) - rand.nextInt(4);
            int mz = z + rand.nextInt(8) - rand.nextInt(8);
            if (my <= 0 || my >= 127) continue;
            uint8_t ground = static_cast<uint8_t>((getBlockSafe(world, mx, my - 1, mz) >> 4) & 0xFF);
            if (getBlockSafe(world, mx, my, mz) != 0 || !world.registry.getById(ground).isOpaque) continue;

            bool shadedFromSky = false;
            for (int sy = my + 1; sy < 128; ++sy) {
                uint8_t aboveId = static_cast<uint8_t>((getBlockSafe(world, mx, sy, mz) >> 4) & 0xFF);
                if (aboveId != 0 && world.registry.getById(aboveId).lightOpacity > 0) {
                    shadedFromSky = true;
                    break;
                }
            }
            if (shadedFromSky) {
                setBlockSafe(world, mx, my, mz, mushState, true);
            }
        }
    }

    // 7. Sugar Canes (Fixed Floating Geometry: strictly enforces canBlockStay per stalk segment)
    static void generateSugarCanePatch(WorldMap& world, JavaRandomB173& rand, int chunkBaseX, int chunkBaseZ) {
        for (int r = 0; r < 20; ++r) {
            int rx = chunkBaseX + rand.nextInt(16) + 8;
            int rz = chunkBaseZ + rand.nextInt(16) + 8;
            int ry = getSurfaceSolidY(world, rx, rz);
            if (ry <= 1 || ry >= 120) continue;

            for (int tryIdx = 0; tryIdx < 20; ++tryIdx) {
                int sx = rx + rand.nextInt(4) - rand.nextInt(4);
                int sz = rz + rand.nextInt(4) - rand.nextInt(4);
                int sy = getSurfaceSolidY(world, sx, sz);
                if (sy <= 1 || sy >= 120) continue;

                if (getBlockSafe(world, sx, sy, sz) == 0 && world.canBlockStay(sx, sy, sz, (83 << 4))) {
                    int reedH = 2 + rand.nextInt(rand.nextInt(3) + 1);
                    for (int h = 0; h < reedH; ++h) {
                        if (getBlockSafe(world, sx, sy + h, sz) == 0 && world.canBlockStay(sx, sy + h, sz, (83 << 4))) {
                            setBlockSafe(world, sx, sy + h, sz, (83 << 4), false);
                        } else {
                            break;
                        }
                    }
                }
            }
        }
    }

    // 8. Pumpkins
    static void generatePumpkinPatch(WorldMap& world, JavaRandomB173& rand, int chunkBaseX, int chunkBaseZ) {
        if (rand.nextInt(32) != 0) return;
        int px = chunkBaseX + rand.nextInt(16) + 8;
        int pz = chunkBaseZ + rand.nextInt(16) + 8;
        int py = getSurfaceSolidY(world, px, pz);
        if (py <= 1 || py >= 120) return;

        for (int p = 0; p < 64; ++p) {
            int bx = px + rand.nextInt(8) - rand.nextInt(8);
            int by = py + rand.nextInt(4) - rand.nextInt(4);
            int bz = pz + rand.nextInt(8) - rand.nextInt(8);
            if (getBlockSafe(world, bx, by, bz) == 0 && ((getBlockSafe(world, bx, by - 1, bz) >> 4) & 0xFF) == 2) {
                uint16_t pMeta = static_cast<uint16_t>(rand.nextInt(4));
                setBlockSafe(world, bx, by, bz, static_cast<uint16_t>((86 << 4) | pMeta), true);
            }
        }
    }

    // 9. Cacti (Fixed Placement: strictly verifies horizontal perimeter clearance)
    static void generateCactusPatch(WorldMap& world, JavaRandomB173& rand, int chunkBaseX, int chunkBaseZ, int attempts) {
        for (int c = 0; c < attempts; ++c) {
            int cxP = chunkBaseX + rand.nextInt(16) + 8;
            int czP = chunkBaseZ + rand.nextInt(16) + 8;
            int cyP = getSurfaceSolidY(world, cxP, czP);
            if (cyP <= 1 || cyP >= 120) continue;

            for (int tryIdx = 0; tryIdx < 10; ++tryIdx) {
                int bx = cxP + rand.nextInt(8) - rand.nextInt(8);
                int bz = czP + rand.nextInt(8) - rand.nextInt(8);
                int by = getSurfaceSolidY(world, bx, bz);
                if (by <= 1 || by >= 120) continue;

                if (getBlockSafe(world, bx, by, bz) == 0 && world.canBlockStay(bx, by, bz, (81 << 4))) {
                    int ch = 1 + rand.nextInt(rand.nextInt(3) + 1);
                    for (int h = 0; h < ch; ++h) {
                        if (getBlockSafe(world, bx, by + h, bz) == 0 && world.canBlockStay(bx, by + h, bz, (81 << 4))) {
                            setBlockSafe(world, bx, by + h, bz, (81 << 4), false);
                        } else {
                            break;
                        }
                    }
                }
            }
        }
    }

    // 10. Spring Wells
    static void generateLiquidSprings(WorldMap& world, JavaRandomB173& rand, int chunkBaseX, int chunkBaseZ) {
        auto generateSpring = [&](uint16_t fluidState, int sx, int sy, int sz) {
            if (sy <= 1 || sy >= 126) return;
            if (((getBlockSafe(world, sx, sy + 1, sz) >> 4) & 0xFF) != 1) return;
            if (((getBlockSafe(world, sx, sy - 1, sz) >> 4) & 0xFF) != 1) return;
            uint8_t curId = static_cast<uint8_t>((getBlockSafe(world, sx, sy, sz) >> 4) & 0xFF);
            if (curId != 0 && curId != 1) return;

            int stoneWalls = 0;
            int airExits   = 0;
            static constexpr int dirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
            for (const auto& d : dirs) {
                uint8_t id = static_cast<uint8_t>((getBlockSafe(world, sx + d[0], sy, sz + d[1]) >> 4) & 0xFF);
                if (id == 1) stoneWalls++;
                else if (id == 0) airExits++;
            }
            if (stoneWalls == 3 && airExits == 1) {
                setBlockSafe(world, sx, sy, sz, fluidState, false);
                world.scheduleTick(sx, sy, sz, ((fluidState >> 4) == 10) ? 15 : 4);
            }
        };

        for (int ws = 0; ws < 50; ++ws) {
            int sx = chunkBaseX + rand.nextInt(16) + 8;
            int sy = rand.nextInt(rand.nextInt(120) + 8);
            int sz = chunkBaseZ + rand.nextInt(16) + 8;
            generateSpring((8 << 4), sx, sy, sz);
        }
        for (int ls = 0; ls < 20; ++ls) {
            int sx = chunkBaseX + rand.nextInt(16) + 8;
            int sy = rand.nextInt(rand.nextInt(rand.nextInt(112) + 8) + 8);
            int sz = chunkBaseZ + rand.nextInt(16) + 8;
            generateSpring((10 << 4), sx, sy, sz);
        }
    }

    // 11. Cryospheric Freezing & Snow Layer Pass (Fixed: canSupportSnow ensures snow NEVER rests on Cacti)
    static void generateSnowAndIcePass(WorldMap& world, int chunkBaseX, int chunkBaseZ,
                                       const float climateTemp[256]) {
        for (int x = chunkBaseX + 8; x < chunkBaseX + 24; ++x) {
            for (int z = chunkBaseZ + 8; z < chunkBaseZ + 24; ++z) {
                int lx = x & 15, lz = z & 15;
                float localTemp = climateTemp[lz * 16 + lx];
                if (localTemp >= 0.5f) continue;

                int topY = getSurfaceSolidY(world, x, z);
                if (topY <= 0 || topY >= 127) continue;

                uint16_t belowSt = getBlockSafe(world, x, topY - 1, z);
                uint8_t belowId  = static_cast<uint8_t>((belowSt >> 4) & 0xFF);

                if (belowId == 9 || belowId == 8) {
                    setBlockSafe(world, x, topY - 1, z, (79 << 4), false);
                } else if (getBlockSafe(world, x, topY, z) == 0 && world.registry.get(belowSt).canSupportSnow()) {
                    setBlockSafe(world, x, topY, z, (78 << 4), false);
                }
            }
        }
    }
};