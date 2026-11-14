#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"


#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

// ============================================================================
// CANONICAL FLUID KINEMATICS & RANDOM TICK SUBSYSTEM (SPEC-2011)
// ============================================================================
// Technical Subsystem Architecture:
// 1. Manifold Fluid Flow Dynamics (Non-Newtonian Cellular Automata):
//    - Elevation Gradient Pathfinding: Fluid cells compute directional flow costs
//      toward the nearest drop-off within a 4-block Manhattan horizon.
//    - Decay Step Propagation: Horizontal displacement increments decay by 1 level
//      for water and non-dimensional lava, or 2 levels for Overworld lava.
//    - Infinite Fluid Genesis: A fluid cell transitions into a permanent source
//      (level 0) when bounded by at least two orthogonal source neighbors overlying
//      a solid substrate or stable fluid bed.
// 2. Thermodynamic Phase Transformations (Hardening & Quenching):
//    - Hydrothermal Basaltification: Lateral or vertical contact between water and
//      a stationary high-temperature fluid cell (level 0) instantly crystallizes
//      into vitreous obsidian (ID 49).
//    - Dynamic Quenching: Contact between water and flowing lava (level > 0)
//      solidifies into igneous cobblestone (ID 4).
//    - Vapor Emission: Hardening reactions discharge localized acoustical transients
//      (fizz audio) with randomized pitch scaling in [2.6, 3.4] kHz.
// 3. Stochastic Vegetation & Thermodynamic Update Cycles (Random Ticks):
//    - Botanical Decay: Canopy leaf foliage executes an iterative 4-step Manhattan
//      breadth-first connectivity search within a 9x9x9 kernel. Detached foliage
//      spontaneously degrades into dropped saplings or fruit.
//    - Agricultural Moisture Equilibrium: Soil hydration evaluates water reservoirs
//      in an intersecting 9x2x9 volumetric neighborhood.
//    - Desiccation & Cryospheric Melting: Exposed snow layers and frozen ice
//      sublimate when nearby artificial block luminosity exceeds (11 - opacity).
// ============================================================================

class WorldFluids {
public:
    static inline int getFluidTickRate(uint8_t blockId, int32_t dimension) {
        if (blockId == 8 || blockId == 9) return 5;
        if (blockId == 10 || blockId == 11) return (dimension == -1) ? 10 : 30;
        return 4;
    }

    static inline uint16_t resolveSupportBreakDrop(uint8_t blockId) {
        if (blockId == 51 || blockId == 78 || blockId == 30 || blockId == 31 || blockId == 92) return 0;
        if (blockId == 55) return ITEM_REDSTONE;
        if (blockId == 93 || blockId == 94) return ITEM_REPEATER_ITEM;
        if (blockId == 83) return ITEM_REEDS;
        if (blockId == 59) return ITEM_SEEDS;
        if (blockId == 64) return ITEM_WOOD_DOOR;
        if (blockId == 71) return ITEM_IRON_DOOR;
        if (blockId == 26) return ITEM_BED_ITEM;
        return blockId;
    }

    template<typename WorldType>
    static void triggerLavaMixEffects(WorldType& world, int wx, int wy, int wz) {
        (void)wx; (void)wy; (void)wz;
        if (world.audioEngine && world.assetManager && !world.assetManager->fizzSound.samples.empty()) {
            world.tickRng = world.tickRng * 1664525u + 1013904223u;
            float r1 = static_cast<float>(world.tickRng >> 8) / 16777216.0f;
            world.tickRng = world.tickRng * 1664525u + 1013904223u;
            float r2 = static_cast<float>(world.tickRng >> 8) / 16777216.0f;
            float pitch = 2.6f + (r1 - r2) * 0.8f;
            world.audioEngine->play2D(world.assetManager->fizzSound, 0.5f, pitch);
        }
    }

    template<typename WorldType>
    static bool checkForHarden(WorldType& world, int wx, int wy, int wz) {
        uint16_t st = world.getBlockAt(wx, wy, wz);
        uint8_t id  = static_cast<uint8_t>((st >> 4) & 0xFF);
        if (id != 10 && id != 11) return false;

        static constexpr int checkDirs[5][3] = { {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}, {0,1,0} };
        bool touchesWater = false;
        for (const auto& d : checkDirs) {
            uint8_t nId = static_cast<uint8_t>((world.getBlockAt(wx + d[0], wy + d[1], wz + d[2]) >> 4) & 0xFF);
            if (nId == 8 || nId == 9) {
                touchesWater = true;
                break;
            }
        }

        if (touchesWater) {
            uint8_t lavaMeta = static_cast<uint8_t>(st & 0x0F);
            uint16_t hardenedState = (lavaMeta == 0) ? (49 << 4) : (4 << 4);
            world.setBlockAt(wx, wy, wz, hardenedState, true);
            triggerLavaMixEffects(world, wx, wy, wz);
            return true;
        }
        return false;
    }

    template<typename WorldType>
    static bool blocksFluidMovement(const WorldType& world, int wx, int wy, int wz) {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return true;
        uint16_t st = world.getBlockAt(wx, wy, wz);
        uint8_t id  = static_cast<uint8_t>((st >> 4) & 0xFF);
        if (id == 0) return false;
        if (id == 64 || id == 71 || id == 63 || id == 68 || id == 65 || id == 83 || id == 90) {
            return true;
        }
        return world.registry.getById(id).isSolid;
    }

    template<typename WorldType>
    static bool canFluidDisplace(const WorldType& world, int wx, int wy, int wz, bool isLava) {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return false;
        uint16_t st = world.getBlockAt(wx, wy, wz);
        uint8_t id  = static_cast<uint8_t>((st >> 4) & 0xFF);
        if (id == 0) return true;
        if (!isLava && (id == 8 || id == 9)) return false;
        if (isLava && (id == 10 || id == 11)) return false;
        if (id == 8 || id == 9 || id == 10 || id == 11) return true;
        return !blocksFluidMovement(world, wx, wy, wz);
    }

    template<typename WorldType>
    static void flowIntoBlock(WorldType& world, int wx, int wy, int wz, int newLevel, bool isLava, bool isVerticalDrop) {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return;
        uint16_t targetSt = world.getBlockAt(wx, wy, wz);
        uint8_t targetId  = static_cast<uint8_t>((targetSt >> 4) & 0xFF);
        uint8_t targetMeta = static_cast<uint8_t>(targetSt & 0x0F);

        if (!isLava && (targetId == 10 || targetId == 11)) {
            uint16_t hardened = (targetMeta == 0) ? (49 << 4) : (4 << 4);
            world.setBlockAt(wx, wy, wz, hardened, true);
            triggerLavaMixEffects(world, wx, wy, wz);
            return;
        }

        if (isLava && isVerticalDrop && (targetId == 8 || targetId == 9)) {
            world.setBlockAt(wx, wy, wz, (1 << 4), true);
            triggerLavaMixEffects(world, wx, wy, wz);
            return;
        }

        if (!canFluidDisplace(world, wx, wy, wz, isLava)) return;

        if (targetId != 0 && targetId != 8 && targetId != 9 && targetId != 10 && targetId != 11) {
            if (!isLava) {
                uint16_t dropId = resolveSupportBreakDrop(targetId);
                if (dropId != 0) {
                    world.pendingDetachedDrops.push_back({ dropId, 0, 1, wx + 0.5f, wy + 0.3f, wz + 0.5f });
                }
            }
        }

        uint8_t flowId = isLava ? 10 : 8;
        world.setBlockAt(wx, wy, wz, static_cast<uint16_t>((flowId << 4) | (newLevel & 0x0F)), true);
        world.scheduleTick(wx, wy, wz, getFluidTickRate(flowId, world.dimension));
    }

    template<typename WorldType>
    static int calculateFlowCost(const WorldType& world, int wx, int wy, int wz, int dist, int fromDir, bool isLava) {
        static constexpr int dirX[4] = { -1, 1,  0, 0 };
        static constexpr int dirZ[4] = {  0, 0, -1, 1 };

        int minCost = 1000;
        for (int dir = 0; dir < 4; ++dir) {
            if ((dir == 0 && fromDir == 1) ||
                (dir == 1 && fromDir == 0) ||
                (dir == 2 && fromDir == 3) ||
                (dir == 3 && fromDir == 2)) {
                continue;
            }

            int nx = wx + dirX[dir];
            int nz = wz + dirZ[dir];
            if (blocksFluidMovement(world, nx, wy, nz)) continue;

            uint16_t st = world.getBlockAt(nx, wy, nz);
            uint8_t id  = static_cast<uint8_t>((st >> 4) & 0xFF);
            uint8_t meta = static_cast<uint8_t>(st & 0x0F);
            bool sameSource = (!isLava && (id == 8 || id == 9) && meta == 0) ||
                              (isLava && (id == 10 || id == 11) && meta == 0);
            if (sameSource) continue;

            if (!blocksFluidMovement(world, nx, wy - 1, nz)) {
                return dist;
            }

            if (dist < 4) {
                int subCost = calculateFlowCost(world, nx, wy, nz, dist + 1, dir, isLava);
                if (subCost < minCost) minCost = subCost;
            }
        }
        return minCost;
    }

    template<typename WorldType>
    static void getOptimalFlowDirections(const WorldType& world, int wx, int wy, int wz, bool isLava, bool outOptimal[4]) {
        static constexpr int dirX[4] = { -1, 1,  0, 0 };
        static constexpr int dirZ[4] = {  0, 0, -1, 1 };

        int flowCost[4] = { 1000, 1000, 1000, 1000 };
        for (int dir = 0; dir < 4; ++dir) {
            int nx = wx + dirX[dir];
            int nz = wz + dirZ[dir];
            if (blocksFluidMovement(world, nx, wy, nz)) continue;

            uint16_t st = world.getBlockAt(nx, wy, nz);
            uint8_t id  = static_cast<uint8_t>((st >> 4) & 0xFF);
            uint8_t meta = static_cast<uint8_t>(st & 0x0F);
            bool sameSource = (!isLava && (id == 8 || id == 9) && meta == 0) ||
                              (isLava && (id == 10 || id == 11) && meta == 0);
            if (sameSource) continue;

            if (!blocksFluidMovement(world, nx, wy - 1, nz)) {
                flowCost[dir] = 1;
            } else {
                flowCost[dir] = calculateFlowCost(world, nx, wy, nz, 2, dir, isLava);
            }
        }

        int minCost = std::min({ flowCost[0], flowCost[1], flowCost[2], flowCost[3] });
        for (int dir = 0; dir < 4; ++dir) {
            outOptimal[dir] = (flowCost[dir] == minCost);
        }
    }

    template<typename WorldType>
    static void tickFluidFlow(WorldType& world, int wx, int wy, int wz, uint16_t state) {
        if (checkForHarden(world, wx, wy, wz)) return;

        uint8_t id      = static_cast<uint8_t>((state >> 4) & 0xFF);
        int     curMeta = static_cast<int>(state & 0x0F);
        bool    isLava  = (id == 10 || id == 11);
        uint8_t flowId  = isLava ? 10 : 8;
        uint8_t stillId = isLava ? 11 : 9;

        int decayStep = (isLava && world.dimension != -1) ? 2 : 1;
        int tickRate  = getFluidTickRate(flowId, world.dimension);

        auto isSameFluid = [isLava](uint8_t qId) -> bool {
            return isLava ? (qId == 10 || qId == 11) : (qId == 8 || qId == 9);
        };

        if (curMeta > 0) {
            int minNeighborDecay = 100;
            int adjacentSources  = 0;

            static constexpr int hDirs[4][2] = { {-1,0}, {1,0}, {0,-1}, {0,1} };
            for (const auto& d : hDirs) {
                uint16_t nSt = world.getBlockAt(wx + d[0], wy, wz + d[1]);
                uint8_t  nId = static_cast<uint8_t>((nSt >> 4) & 0xFF);
                if (isSameFluid(nId)) {
                    int nMeta = static_cast<int>(nSt & 0x0F);
                    if (!isLava && nMeta == 0) {
                        adjacentSources++;
                    }
                    int effDecay = (nMeta >= 8) ? 0 : nMeta;
                    if (effDecay < minNeighborDecay) minNeighborDecay = effDecay;
                }
            }

            int nextMeta = minNeighborDecay + decayStep;
            if (nextMeta >= 8 || minNeighborDecay < 0) {
                nextMeta = -1;
            }

            uint16_t aboveSt = world.getBlockAt(wx, wy + 1, wz);
            uint8_t  aboveId = static_cast<uint8_t>((aboveSt >> 4) & 0xFF);
            if (isSameFluid(aboveId)) {
                nextMeta = 8;
            }

            // Infinite Fluid Genesis: 2+ lateral source neighbors over solid substrate or source
            if (!isLava && adjacentSources >= 2) {
                uint16_t belowSt = world.getBlockAt(wx, wy - 1, wz);
                uint8_t  belowId = static_cast<uint8_t>((belowSt >> 4) & 0xFF);
                if (world.registry.getById(belowId).isSolid || (isSameFluid(belowId) && (belowSt & 0x0F) == 0)) {
                    nextMeta = 0;
                }
            }

            if (nextMeta != curMeta) {
                curMeta = nextMeta;
                if (curMeta < 0) {
                    world.setBlockAt(wx, wy, wz, 0, true);
                    return;
                }
                uint8_t targetBlockId = (curMeta == 0 && !isLava) ? stillId : flowId;
                world.setBlockAt(wx, wy, wz, static_cast<uint16_t>((targetBlockId << 4) | (curMeta & 0x0F)), true);
                world.scheduleTick(wx, wy, wz, tickRate);
            } else if (id == flowId) {
                world.setBlockAt(wx, wy, wz, static_cast<uint16_t>((stillId << 4) | (curMeta & 0x0F)), false);
            }
        } else if (id == flowId) {
            world.setBlockAt(wx, wy, wz, static_cast<uint16_t>((stillId << 4) | 0), false);
        }

        // Vertical Gravity Descent
        if (wy > 0) {
            uint16_t belowSt = world.getBlockAt(wx, wy - 1, wz);
            uint8_t  belowId = static_cast<uint8_t>((belowSt >> 4) & 0xFF);

            if (isSameFluid(belowId)) {
                int belowMeta = static_cast<int>(belowSt & 0x0F);
                if (belowMeta < 8) {
                    int fallingMeta = belowMeta | 8;
                    world.setBlockAt(wx, wy - 1, wz, static_cast<uint16_t>((flowId << 4) | fallingMeta), true);
                    world.scheduleTick(wx, wy - 1, wz, tickRate);
                }
            } else if (canFluidDisplace(world, wx, wy - 1, wz, isLava)) {
                flowIntoBlock(world, wx, wy - 1, wz, 8, isLava, true);
            }
        }

        // Horizontal Dispersion along least-action elevation gradients
        uint16_t belowSt = (wy > 0) ? world.getBlockAt(wx, wy - 1, wz) : (7 << 4);
        uint8_t  belowId = static_cast<uint8_t>((belowSt >> 4) & 0xFF);
        bool canSpreadHoriz = (curMeta == 0) || !canFluidDisplace(world, wx, wy - 1, wz, isLava) || isSameFluid(belowId);

        if (canSpreadHoriz) {
            int spreadLevel = (curMeta >= 8) ? decayStep : (curMeta + decayStep);
            if (spreadLevel >= 8) return;

            bool optimalDirs[4]{};
            getOptimalFlowDirections(world, wx, wy, wz, isLava, optimalDirs);

            static constexpr int dirX[4] = { -1, 1,  0, 0 };
            static constexpr int dirZ[4] = {  0, 0, -1, 1 };

            for (int dir = 0; dir < 4; ++dir) {
                if (!optimalDirs[dir]) continue;
                int nx = wx + dirX[dir];
                int nz = wz + dirZ[dir];
                uint16_t adjSt = world.getBlockAt(nx, wy, nz);
                uint8_t  adjId = static_cast<uint8_t>((adjSt >> 4) & 0xFF);
                if (isSameFluid(adjId)) {
                    int adjMeta = static_cast<int>(adjSt & 0x0F);
                    if (adjMeta > spreadLevel && adjMeta < 8) {
                        world.setBlockAt(nx, wy, nz, static_cast<uint16_t>((flowId << 4) | spreadLevel), true);
                        world.scheduleTick(nx, wy, nz, tickRate);
                    }
                } else {
                    flowIntoBlock(world, nx, wy, nz, spreadLevel, isLava, false);
                }
            }
        }
    }

    template<typename WorldType>
    static bool isLeafConnectedToLog(const WorldType& world, int wx, int wy, int wz) {
        uint8_t gridCache[9][9][9]{};
        for (int dy = -4; dy <= 4; ++dy) {
            int y = wy + dy;
            if (y < 0 || y >= CHUNK_HEIGHT) continue;
            for (int dz = -4; dz <= 4; ++dz) {
                for (int dx = -4; dx <= 4; ++dx) {
                    uint8_t id = static_cast<uint8_t>((world.getBlockAt(wx + dx, y, wz + dz) >> 4) & 0xFF);
                    if (id == 17)      gridCache[dx + 4][dy + 4][dz + 4] = 2;
                    else if (id == 18) gridCache[dx + 4][dy + 4][dz + 4] = 1;
                }
            }
        }

        static constexpr int dirs[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
        for (int pass = 1; pass <= 4; ++pass) {
            uint8_t targetVal = static_cast<uint8_t>(2 + pass);
            for (int x = 0; x < 9; ++x) {
                for (int y = 0; y < 9; ++y) {
                    for (int z = 0; z < 9; ++z) {
                        if (gridCache[x][y][z] != targetVal) continue;
                        for (const auto& d : dirs) {
                            int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                            if (nx < 0 || nx >= 9 || ny < 0 || ny >= 9 || nz < 0 || nz >= 9) continue;
                            if (gridCache[nx][ny][nz] == 2) return true;
                            if (gridCache[nx][ny][nz] == 1) gridCache[nx][ny][nz] = static_cast<uint8_t>(targetVal + 1);
                        }
                    }
                }
            }
        }
        return false;
    }

    template<typename WorldType>
    static void tickFireBlock(WorldType& world, int wx, int wy, int wz, uint16_t state) {
        if (!world.canBlockStay(wx, wy, wz, state)) {
            world.setBlockAt(wx, wy, wz, 0, true);
            return;
        }

        uint8_t belowId = static_cast<uint8_t>((world.getBlockAt(wx, wy - 1, wz) >> 4) & 0xFF);
        bool infiniburn = (belowId == 87); // Netherrack sustains combustion indefinitely
        uint8_t age     = static_cast<uint8_t>(state & 0x0F);

        if (!infiniburn && age == 15) {
            world.tickRng = world.tickRng * 1664525u + 1013904223u;
            if ((world.tickRng & 3u) == 0) {
                world.setBlockAt(wx, wy, wz, 0, true);
                return;
            }
        }

        if (age < 15) {
            world.tickRng = world.tickRng * 1664525u + 1013904223u;
            uint8_t nextAge = static_cast<uint8_t>(std::min(15, static_cast<int>(age) + static_cast<int>((world.tickRng >> 8) & 3u)));
            world.setBlockAt(wx, wy, wz, static_cast<uint16_t>((51 << 4) | nextAge), false);
        }

        static constexpr int dirs[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
        for (const auto& d : dirs) {
            int nx = wx + d[0], ny = wy + d[1], nz = wz + d[2];
            uint16_t nSt = world.getBlockAt(nx, ny, nz);
            if (nSt != 0 && world.registry.get(nSt).isFlammable) {
                world.tickRng = world.tickRng * 1664525u + 1013904223u;
                if (((world.tickRng >> 8) % 10u) == 0) {
                    world.setBlockAt(nx, ny, nz, (51 << 4), true);
                }
            }
        }

        world.scheduleTick(wx, wy, wz, 40);
    }

    template<typename WorldType>
    static void tickRandomBlock(WorldType& world, int wx, int wy, int wz, uint16_t state) {
        uint8_t id   = static_cast<uint8_t>((state >> 4) & 0xFF);
        uint8_t meta = static_cast<uint8_t>(state & 0x0F);

        // 1. Grass Block (2): Desiccation under darkness or lateral colony expansion
        if (id == 2 && wy + 1 < CHUNK_HEIGHT) {
            uint16_t above = world.getBlockAt(wx, wy + 1, wz);
            int lightAbove = std::max(static_cast<int>(world.getBlockLightAt(wx, wy + 1, wz)),
                                      static_cast<int>(world.getSkyLightAt(wx, wy + 1, wz)));
            if (lightAbove < 4 && world.registry.get(above).lightOpacity > 2) {
                world.setBlockAt(wx, wy, wz, (3 << 4), false);
            } else if (lightAbove >= 9) {
                for (int attempt = 0; attempt < 4; ++attempt) {
                    world.tickRng = world.tickRng * 1664525u + 1013904223u;
                    int tx = wx + static_cast<int>((world.tickRng >> 8) % 3u) - 1;
                    int ty = wy + static_cast<int>((world.tickRng >> 12) % 5u) - 3;
                    int tz = wz + static_cast<int>((world.tickRng >> 16) % 3u) - 1;
                    if (ty <= 0 || ty + 1 >= CHUNK_HEIGHT) continue;

                    if (((world.getBlockAt(tx, ty, tz) >> 4) & 0xFF) == 3) {
                        int tLight = std::max(static_cast<int>(world.getBlockLightAt(tx, ty + 1, tz)),
                                              static_cast<int>(world.getSkyLightAt(tx, ty + 1, tz)));
                        if (tLight >= 4 && world.registry.get(world.getBlockAt(tx, ty + 1, tz)).lightOpacity <= 2) {
                            world.setBlockAt(tx, ty, tz, (2 << 4), false);
                        }
                    }
                }
            }
            return;
        }

        // 2. Farmland (60): 9x2x9 Hydration equilibrium or arid reversion to dirt
        if (id == 60) {
            bool nearWater = false;
            for (int dx = -4; dx <= 4 && !nearWater; ++dx) {
                for (int dy = 0; dy <= 1 && !nearWater; ++dy) {
                    for (int dz = -4; dz <= 4; ++dz) {
                        uint8_t wId = static_cast<uint8_t>((world.getBlockAt(wx + dx, wy + dy, wz + dz) >> 4) & 0xFF);
                        if (wId == 8 || wId == 9) {
                            nearWater = true;
                            break;
                        }
                    }
                }
            }
            if (nearWater) {
                if (meta != 7) {
                    world.setBlockAt(wx, wy, wz, (60 << 4) | 7, false);
                }
            } else if (meta > 0) {
                world.setBlockAt(wx, wy, wz, static_cast<uint16_t>((60 << 4) | (meta - 1)), false);
            } else if (((world.getBlockAt(wx, wy + 1, wz) >> 4) & 0xFF) != 59) {
                world.setBlockAt(wx, wy, wz, (3 << 4), true);
            }
            return;
        }

        // 3. Agricultural Crops (59): Photosynthetic acceleration
        if (id == 59 && meta < 7 && wy + 1 < CHUNK_HEIGHT) {
            int light = std::max(static_cast<int>(world.getBlockLightAt(wx, wy + 1, wz)),
                                 static_cast<int>(world.getSkyLightAt(wx, wy + 1, wz)));
            if (light >= 9) {
                float rate = 1.0f;
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        uint16_t gSt = world.getBlockAt(wx + dx, wy - 1, wz + dz);
                        float bonus = 0.0f;
                        if (((gSt >> 4) & 0xFF) == 60) {
                            bonus = ((gSt & 0x0F) > 0) ? 3.0f : 1.0f;
                        }
                        if (dx != 0 || dz != 0) bonus *= 0.25f;
                        rate += bonus;
                    }
                }
                int mod = std::max(1, static_cast<int>(25.0f / rate) + 1);
                world.tickRng = world.tickRng * 1664525u + 1013904223u;
                if ((static_cast<int>(world.tickRng >> 8) % mod) == 0) {
                    world.setBlockAt(wx, wy, wz, static_cast<uint16_t>((59 << 4) | (meta + 1)), false);
                }
            }
            return;
        }

        // 4. Sugar Canes (83) & Cactus (81): Vertical cellular extension up to 3 blocks
        if ((id == 83 || id == 81) && wy + 1 < CHUNK_HEIGHT && world.getBlockAt(wx, wy + 1, wz) == 0) {
            int heightBelow = 1;
            while (wy - heightBelow >= 0 && ((world.getBlockAt(wx, wy - heightBelow, wz) >> 4) & 0xFF) == id) {
                heightBelow++;
            }
            if (heightBelow < 3) {
                if (meta == 15) {
                    uint16_t newPlant = static_cast<uint16_t>(id << 4);
                    if (world.canBlockStay(wx, wy + 1, wz, newPlant)) {
                        world.setBlockAt(wx, wy + 1, wz, newPlant, true);
                    }
                    world.setBlockAt(wx, wy, wz, static_cast<uint16_t>(id << 4), false);
                } else {
                    world.setBlockAt(wx, wy, wz, static_cast<uint16_t>((id << 4) | (meta + 1)), false);
                }
            }
            return;
        }

        // 5. Tree Leaves (18): 4-block Manhattan log connectivity check
        if (id == 18 && (meta & 0x08) != 0 && (meta & 0x04) == 0) {
            if (isLeafConnectedToLog(world, wx, wy, wz)) {
                world.setBlockAt(wx, wy, wz, static_cast<uint16_t>((18 << 4) | (meta & ~0x08)), false);
            } else {
                uint8_t treeType = meta & 0x03;
                world.setBlockAt(wx, wy, wz, 0, true);
                world.tickRng = world.tickRng * 1664525u + 1013904223u;
                int roll = static_cast<int>((world.tickRng >> 8) % 200u);
                if (roll < 10) {
                    world.pendingDetachedDrops.push_back({ 6, treeType, 1, wx + 0.5f, wy + 0.5f, wz + 0.5f });
                } else if (treeType == 0 && roll == 10) {
                    world.pendingDetachedDrops.push_back({ ITEM_APPLE, 0, 1, wx + 0.5f, wy + 0.5f, wz + 0.5f });
                }
            }
            return;
        }

        // 6. Snow Layer (78) & Ice (79): Thermal phase change near high-intensity artificial light (> 11 - opacity)
        if ((id == 78 || id == 79) && world.getBlockLightAt(wx, wy, wz) > (11 - world.registry.getById(id).lightOpacity)) {
            if (id == 78) {
                world.setBlockAt(wx, wy, wz, 0, true);
            } else {
                world.setBlockAt(wx, wy, wz, (world.dimension == -1) ? 0 : (9 << 4), true);
            }
            return;
        }

        // 7. Thermal Flame (51): Atmospheric consumption
        if (id == 51) {
            tickFireBlock(world, wx, wy, wz, state);
        }
    }
};