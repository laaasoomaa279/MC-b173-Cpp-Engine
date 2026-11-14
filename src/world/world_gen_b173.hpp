#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "world/world.hpp"
#include "world/mcregion_nbt.hpp"
#include "world/noise_b173.hpp"
#include "world/world_features.hpp"
#include "gameplay/combat_nether.hpp"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>
#include <filesystem>

// ============================================================================
// CANONICAL PROCEDURAL CONTINENTAL VOXEL TERRAIN GENERATOR (SPEC-2011)
// ============================================================================
// Technical Subsystem Architecture:
// 1. Dual 2D Thermodynamic Climate Simulation:
//    - Continental temperature and humidity scalar fields sampled via 2D Simplex
//      noise arrays (NoiseGeneratorOctaves2) with octaves keyed to deterministic seed hashes:
//        * tempNoise   : 4 octaves, seed * 9871L,   freq = (double)0.025f, lac = 0.25
//        * rainNoise   : 4 octaves, seed * 39811L,  freq = (double)0.05f,  lac = 0.25
//        * detailNoise : 2 octaves, seed * 543321L, freq = (double)0.25f,  lac = 0.50
//    - Thermodynamic Classification: Biome boundaries resolved via continuous lookup
//      mapping temperature T in [0, 1] and effective rain R = rain * temp in [0, 1].
// 2. Trilinear 3D Density Grid Synthesis (5x17x5 Coarse Interpolation):
//    - Vertical column density sampled across a sub-sampled 5x17x5 lattice spanning
//      horizontal resolution of 4 blocks and vertical steps of 8 blocks.
//    - Density Equation:
//        density = lerp(minLimit, maxLimit, mainNoise) - targetY_attenuation
//    - Trilinear interpolation propagates continuous floating islands, overhangs,
//      and mountain cliffs down to individual 16x128x16 chunk voxel volumes.
//    - Singularities (Far Lands): Coordinate scaling arithmetic undergoes signed
//      32-bit integer overflow when |X| or |Z| >= 12,550,824 blocks, triggering
//      massive continuous vertical cliff saturation.
// 3. Subterranean Speleological Excavation (8-Chunk Ray-Marched Worm Caves):
//    - Cylindrical and spherical cavern voids carved along 3D flight trajectories
//      controlled by polar pitch/yaw derivatives. Cave passes strictly bypass
//      hydrothermal surface bodies (water / stationary ocean fluid).
// 4. Staged Multi-Threaded Population Pipeline:
//    - Pipeline States: CHUNK_EMPTY -> CHUNK_GENERATING -> CHUNK_TERRAIN ->
//      CHUNK_DECORATING -> CHUNK_READY.
//    - Population Offset Invariant: Feature distribution is evaluated at (+8, +8)
//      voxel offsets only when all eight orthogonal and diagonal neighbor chunks
//      reach at least CHUNK_TERRAIN, eliminating seam artifacts and thread race locks.
// ============================================================================

struct SavedWorldSummary {
    std::string   folderName;
    std::string   fullPath;
    BetaLevelData levelData;
};

enum BetaBiomeId : uint8_t {
    BIOME_RAINFOREST      = 0,
    BIOME_SWAMPLAND       = 1,
    BIOME_SEASONAL_FOREST = 2,
    BIOME_FOREST          = 3,
    BIOME_SAVANNA         = 4,
    BIOME_SHRUBLAND       = 5,
    BIOME_TAIGA           = 6,
    BIOME_DESERT          = 7,
    BIOME_PLAINS          = 8,
    BIOME_ICE_DESERT      = 9,
    BIOME_TUNDRA          = 10
};

struct BetaWorkerScratchpad {
    double      densityField[5 * 17 * 5]{};
    BetaBiomeId biomes[16 * 16]{};
    bool        lakeGrid[16 * 16 * 8]{};
    bool        prioritySet = false;
};

inline BetaWorkerScratchpad& getBetaScratchpad() {
    thread_local BetaWorkerScratchpad pad;
    if (!pad.prioritySet) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        pad.prioritySet = true;
    }
    return pad;
}

class Beta173WorldGenerator {
public:
    int64_t              seed = 123456789LL;

    // Canonical Procedural Noise Field Generators
    OctaveNoiseB173      minLimitPerlin; // 16 octaves
    OctaveNoiseB173      maxLimitPerlin; // 16 octaves
    OctaveNoiseB173      mainPerlin;     // 8 octaves
    OctaveNoiseB173      beachNoise;     // 4 octaves
    OctaveNoiseB173      surfaceNoise;   // 4 octaves
    OctaveNoiseB173      scaleNoise;     // 10 octaves
    OctaveNoiseB173      depthNoise;     // 16 octaves
    OctaveNoiseB173      forestNoise;    // 8 octaves

    OctaveSimplex2DB173  tempNoise;      // 4 octaves, seed * 9871L
    OctaveSimplex2DB173  rainNoise;      // 4 octaves, seed * 39811L
    OctaveSimplex2DB173  detailNoise;    // 2 octaves, seed * 543321L

    std::vector<ChunkCoord> spawnCoreChunks;
    std::vector<ChunkCoord> spawnDispatchChunks;
    bool                    spawnDispatched           = false;
    bool                    buildArrivalPortalOnReady = false;
    int32_t                 arrivalPortalX            = 0;
    int32_t                 arrivalPortalY            = 64;
    int32_t                 arrivalPortalZ            = 0;

    explicit Beta173WorldGenerator(int64_t worldSeed = 123456789LL) {
        setSeed(worldSeed);
    }

    void setSeed(int64_t worldSeed) {
        seed = worldSeed;

        JavaRandomB173 rand(seed);
        minLimitPerlin.init(rand, 16);
        maxLimitPerlin.init(rand, 16);
        mainPerlin.init(rand, 8);
        beachNoise.init(rand, 4);
        surfaceNoise.init(rand, 4);
        scaleNoise.init(rand, 10);
        depthNoise.init(rand, 16);
        forestNoise.init(rand, 8);

        JavaRandomB173 tempRand(seed * 9871LL);
        tempNoise.init(tempRand, 4);

        JavaRandomB173 rainRand(seed * 39811LL);
        rainNoise.init(rainRand, 4);

        JavaRandomB173 detailRand(seed * 543321LL);
        detailNoise.init(detailRand, 2);
    }

    static BetaBiomeId getBiomeFromClimate(float temp, float rain) {
        rain *= temp;
        if (temp < 0.1f) return BIOME_TUNDRA;
        if (rain < 0.2f) {
            if (temp < 0.5f) return BIOME_TUNDRA;
            if (temp < 0.95f) return BIOME_SAVANNA;
            return BIOME_DESERT;
        }
        if (rain > 0.5f && temp < 0.7f) return BIOME_SWAMPLAND;
        if (temp < 0.5f) return BIOME_TAIGA;
        if (temp < 0.97f) {
            return (rain < 0.35f) ? BIOME_SHRUBLAND : BIOME_FOREST;
        }
        if (rain < 0.45f) return BIOME_PLAINS;
        if (rain < 0.9f)  return BIOME_SEASONAL_FOREST;
        return BIOME_RAINFOREST;
    }

    void sampleClimateAt(double wx, double wz, float& outTemp, float& outRain) const {
        static constexpr double TEMP_SCALE   = static_cast<double>(0.025f);
        static constexpr double RAIN_SCALE   = static_cast<double>(0.05f);
        static constexpr double DETAIL_SCALE = static_cast<double>(0.25f);

        double tRaw = tempNoise.sample(wx, wz, TEMP_SCALE, 0.25, 0.5);
        double rRaw = rainNoise.sample(wx, wz, RAIN_SCALE, 0.25, 0.5);
        double dRaw = detailNoise.sample(wx, wz, DETAIL_SCALE, 0.5, 0.5);

        double d  = dRaw * 1.1 + 0.5;
        double d1 = (tRaw * 0.15 + 0.7) * 0.99 + d * 0.01;
        d1 = 1.0 - (1.0 - d1) * (1.0 - d1);
        outTemp = static_cast<float>(std::clamp(d1, 0.0, 1.0));

        double d2 = (rRaw * 0.15 + 0.5) * 0.998 + d * 0.002;
        outRain = static_cast<float>(std::clamp(d2, 0.0, 1.0));
    }

    static std::vector<SavedWorldSummary> listSavedWorlds(const std::string& savesRoot = "saves") {
        std::vector<SavedWorldSummary> result;
        std::filesystem::create_directories(savesRoot);
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(savesRoot, ec)) {
            if (!entry.is_directory(ec)) continue;
            SavedWorldSummary sum;
            sum.folderName = entry.path().filename().string();
            sum.fullPath   = entry.path().string();
            sum.levelData.levelName = sum.folderName;
            McRegionStorage::loadLevelDat(sum.fullPath, sum.levelData);
            result.push_back(std::move(sum));
        }
        return result;
    }

    void beginAreaGeneration(int32_t centerCx, int32_t centerCz, int coreRadius = 1,
                             bool withArrivalPortal = false,
                             int32_t portalWx = 0, int32_t portalWy = 64, int32_t portalWz = 0) {
        int safeR = std::clamp(coreRadius, 1, 3);
        spawnCoreChunks.clear();
        spawnDispatchChunks.clear();
        spawnDispatched           = false;
        buildArrivalPortalOnReady = withArrivalPortal;
        arrivalPortalX            = portalWx;
        arrivalPortalY            = portalWy;
        arrivalPortalZ            = portalWz;

        for (int r = 0; r <= safeR + 1; ++r) {
            for (int dz = -r; dz <= r; ++dz) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::abs(dx) == r || std::abs(dz) == r) {
                        ChunkCoord cc{ centerCx + dx, centerCz + dz };
                        spawnDispatchChunks.push_back(cc);
                        if (r <= safeR) spawnCoreChunks.push_back(cc);
                    }
                }
            }
        }
    }

    template<typename QueueType>
    bool stepAreaGeneration(WorldMap& world, WorkerPool& pool, QueueType& meshQueue, float& outProgress) {
        if (spawnCoreChunks.empty()) {
            outProgress = 1.0f;
            return true;
        }
        if (!spawnDispatched) {
            for (const ChunkCoord& cc : spawnDispatchChunks) {
                dispatchChunkBuild(world, pool, cc.cx, cc.cz, meshQueue, true);
            }
            spawnDispatched = true;
        }
        size_t ready = 0;
        for (const ChunkCoord& cc : spawnCoreChunks) {
            if (world.getReadyChunk(cc.cx, cc.cz) != nullptr) ready++;
        }
        outProgress = static_cast<float>(ready) / static_cast<float>(spawnCoreChunks.size());
        if (ready >= spawnCoreChunks.size()) {
            if (buildArrivalPortalOnReady) {
                BetaCombatAndNetherSystem::buildArrivalPortal(world, arrivalPortalX, arrivalPortalY, arrivalPortalZ);
                meshQueue.push({ arrivalPortalX >> 4, arrivalPortalZ >> 4 });
                buildArrivalPortalOnReady = false;
            }
            spawnCoreChunks.clear();
            spawnDispatchChunks.clear();
            spawnDispatched = false;
            outProgress = 1.0f;
            return true;
        }
        return false;
    }

    template<typename QueueType>
    void updateInfiniteStreaming(WorldMap& world, WorkerPool& pool, float playerX, float playerZ, QueueType& meshQueue) {
        int newCx = (static_cast<int>(std::floor(playerX))) >> 4;
        int newCz = (static_cast<int>(std::floor(playerZ))) >> 4;
        world.centerChunkX = newCx;
        world.centerChunkZ = newCz;

        size_t maxBacklog = std::max<size_t>(4, static_cast<size_t>(pool.threadCount()) * 2);
        if (pool.pendingCount() >= maxBacklog) return;

        int maxR = std::min(world.renderDistance + 1, (GRID_DIM / 2) - 2);
        int dispatched = 0;

        for (int r = 0; r <= maxR && dispatched < 6; ++r) {
            for (int dz = -r; dz <= r && dispatched < 6; ++dz) {
                for (int dx = -r; dx <= r && dispatched < 6; ++dx) {
                    if (std::abs(dx) != r && std::abs(dz) != r) continue;
                    int32_t cx = newCx + dx, cz = newCz + dz;
                    ChunkColumn& slot = world.getSlot(cx, cz);
                    uint8_t st = slot.state.load(std::memory_order_acquire);
                    if (slot.chunkX == cx && slot.chunkZ == cz && st != CHUNK_EMPTY) continue;
                    if (st == CHUNK_GENERATING || st == CHUNK_DECORATING) continue;

                    dispatchChunkBuild(world, pool, cx, cz, meshQueue, r <= 2);
                    dispatched++;
                }
            }
        }
    }

    template<typename QueueType>
    void dispatchChunkBuild(WorldMap& world, WorkerPool& pool, int32_t cx, int32_t cz,
                            QueueType& meshQueue, bool highPriority) {
        int32_t activeDim = world.dimension;
        std::string dimSaveDir = world.getDimensionSaveDir();

        {
            std::lock_guard<std::mutex> lock(world.getChunkLock(cx, cz));
            ChunkColumn& slot = world.getSlot(cx, cz);
            uint8_t st = slot.state.load(std::memory_order_acquire);
            if (slot.chunkX == cx && slot.chunkZ == cz && st != CHUNK_EMPTY) return;
            if (st == CHUNK_GENERATING || st == CHUNK_DECORATING) return;

            if (st == CHUNK_READY && slot.dirty) {
                McRegionChunkData oldMcr;
                world.exportChunkToMcRegion(slot, oldMcr);
                McRegionStorage::saveChunk(dimSaveDir, oldMcr);
            }

            slot.chunkX = cx;
            slot.chunkZ = cz;
            slot.dirty  = false;
            slot.state.store(CHUNK_GENERATING, std::memory_order_release);
        }

        pool.submit([this, &world, cx, cz, activeDim, dimSaveDir, &meshQueue]() {
            if (world.dimension != activeDim) return;

            BetaWorkerScratchpad& pad = getBetaScratchpad();
            ChunkColumn& slot = world.getSlot(cx, cz);
            if (slot.chunkX != cx || slot.chunkZ != cz) return;

            McRegionChunkData savedMcr;
            if (McRegionStorage::loadChunk(dimSaveDir, cx, cz, savedMcr)) {
                world.importChunkFromMcRegion(savedMcr, slot);
                notifyNeighborsReady(world, cx, cz, meshQueue);
                return;
            }

            if (activeDim == -1) {
                generateNetherBase(slot, cx, cz, pad);
            } else {
                generateOverworldBase(slot, cx, cz, pad);
            }
            slot.state.store(CHUNK_TERRAIN, std::memory_order_release);

            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    tryPromoteAndDecorate(world, cx + dx, cz + dz, pad, meshQueue);
                }
            }
        }, highPriority);
    }

private:
    void generateOverworldBase(ChunkColumn& col, int32_t cx, int32_t cz, BetaWorkerScratchpad& pad) const {
        col.sectionMask = 0xFF;
        for (int s = 0; s < SECTIONS_PER_COL; ++s) col.sections[s].clear(0);

        for (int lx = 0; lx < 16; ++lx) {
            for (int lz = 0; lz < 16; ++lz) {
                float t = 0.5f, r = 0.5f;
                sampleClimateAt(static_cast<double>(cx * 16 + lx), static_cast<double>(cz * 16 + lz), t, r);
                col.temperature[lz * 16 + lx] = t;
                col.humidity[lz * 16 + lx]    = r;
                pad.biomes[lx * 16 + lz]      = getBiomeFromClimate(t, r);
            }
        }

        int baseX = cx * 4, baseZ = cz * 4;
        for (int x = 0; x < 5; ++x) {
            int biomeX = x * 3 + 1;
            for (int z = 0; z < 5; ++z) {
                int biomeZ = z * 3 + 1;
                int gx = baseX + x, gz = baseZ + z;

                double temp = static_cast<double>(col.temperature[biomeZ * 16 + biomeX]);
                double rain = static_cast<double>(col.humidity[biomeZ * 16 + biomeX]) * temp;
                double climateFactor = 1.0 - std::pow(1.0 - rain, 4.0);

                double sVal = (scaleNoise.sample2D(gx, gz, 1.121, 1.121) + 256.0) / 512.0 * climateFactor;
                if (sVal > 1.0) sVal = 1.0;

                double dVal = depthNoise.sample2D(gx, gz, 200.0, 200.0) / 8000.0;
                if (dVal < 0.0) dVal = -dVal * 0.3;
                dVal = dVal * 3.0 - 2.0;
                if (dVal < 0.0) {
                    dVal *= 0.5;
                    if (dVal < -1.0) dVal = -1.0;
                    dVal /= 1.4;
                    dVal *= 0.5;
                    sVal = 0.0;
                } else {
                    if (dVal > 1.0) dVal = 1.0;
                    dVal /= 8.0;
                }

                if (sVal < 0.0) sVal = 0.0;
                sVal += 0.5;
                dVal = dVal * 17.0 / 16.0;
                double targetY = 8.5 + dVal * 4.0;

                for (int y = 0; y < 17; ++y) {
                    double yOff = (static_cast<double>(y) - targetY) * 12.0 / sVal;
                    if (yOff < 0.0) yOff *= 4.0;

                    double minV  = minLimitPerlin.sample3D(gx, y, gz, 684.412, 684.412, 684.412) / 512.0;
                    double maxV  = maxLimitPerlin.sample3D(gx, y, gz, 684.412, 684.412, 684.412) / 512.0;
                    double mainV = (mainPerlin.sample3D(gx, y, gz, 684.412 / 80.0, 684.412 / 160.0, 684.412 / 80.0) / 10.0 + 1.0) * 0.5;

                    double dens = (mainV < 0.0) ? minV : ((mainV > 1.0) ? maxV : (minV + (maxV - minV) * mainV));
                    dens -= yOff;
                    if (y > 13) {
                        double slide = static_cast<double>(y - 13) / 3.0;
                        dens = dens * (1.0 - slide) + (-10.0 * slide);
                    }
                    pad.densityField[(x * 5 + z) * 17 + y] = dens;
                }
            }
        }

        for (int cxCell = 0; cxCell < 4; ++cxCell) {
            for (int czCell = 0; czCell < 4; ++czCell) {
                for (int cyCell = 0; cyCell < 16; ++cyCell) {
                    double d000 = pad.densityField[((cxCell + 0) * 5 + (czCell + 0)) * 17 + (cyCell + 0)];
                    double d001 = pad.densityField[((cxCell + 0) * 5 + (czCell + 1)) * 17 + (cyCell + 0)];
                    double d100 = pad.densityField[((cxCell + 1) * 5 + (czCell + 0)) * 17 + (cyCell + 0)];
                    double d101 = pad.densityField[((cxCell + 1) * 5 + (czCell + 1)) * 17 + (cyCell + 0)];

                    double d010 = (pad.densityField[((cxCell + 0) * 5 + (czCell + 0)) * 17 + (cyCell + 1)] - d000) * 0.125;
                    double d011 = (pad.densityField[((cxCell + 0) * 5 + (czCell + 1)) * 17 + (cyCell + 1)] - d001) * 0.125;
                    double d110 = (pad.densityField[((cxCell + 1) * 5 + (czCell + 0)) * 17 + (cyCell + 1)] - d100) * 0.125;
                    double d111 = (pad.densityField[((cxCell + 1) * 5 + (czCell + 1)) * 17 + (cyCell + 1)] - d101) * 0.125;

                    for (int sy = 0; sy < 8; ++sy) {
                        int wy = cyCell * 8 + sy;
                        double v00 = d000, v01 = d001;
                        double v10 = (d100 - d000) * 0.25, v11 = (d101 - d001) * 0.25;

                        for (int sx = 0; sx < 4; ++sx) {
                            int lx = cxCell * 4 + sx;
                            double dens = v00, stepZ = (v01 - v00) * 0.25;
                            for (int sz = 0; sz < 4; ++sz) {
                                int lz = czCell * 4 + sz;
                                if (dens > 0.0) {
                                    col.sections[wy >> 4].setBlock(lx, wy & 15, lz, (1 << 4));
                                } else if (wy < SEA_LEVEL) {
                                    col.sections[wy >> 4].setBlock(lx, wy & 15, lz, (9 << 4));
                                }
                                dens += stepZ;
                            }
                            v00 += v10;
                            v01 += v11;
                        }
                        d000 += d010; d001 += d011; d100 += d110; d101 += d111;
                    }
                }
            }
        }

        JavaRandomB173 rand(static_cast<int64_t>(cx) * 341873128712LL + static_cast<int64_t>(cz) * 132897987541LL);
        for (int lx = 0; lx < 16; ++lx) {
            for (int lz = 0; lz < 16; ++lz) {
                int wx = cx * 16 + lx, wz = cz * 16 + lz;
                BetaBiomeId biome = pad.biomes[lx * 16 + lz];
                bool isDesert = (biome == BIOME_DESERT || biome == BIOME_ICE_DESERT);

                bool sandBeach   = (beachNoise.sample2D(wx, wz, 0.03125, 0.03125) + rand.nextDouble() * 0.2) > 0.0;
                bool gravelBeach = (beachNoise.sample2D(wz, wx, 0.03125, 0.03125) + rand.nextDouble() * 0.2) > 3.0;
                int depth = static_cast<int>(surfaceNoise.sample2D(wx, wz, 0.0625, 0.0625) / 3.0 + 3.0 + rand.nextDouble() * 0.25);

                int rem = -1;
                uint16_t topBlk = isDesert ? (12 << 4) : (2 << 4);
                uint16_t filBlk = isDesert ? (12 << 4) : (3 << 4);

                for (int wy = 127; wy >= 0; --wy) {
                    int sy = wy >> 4;
                    if (wy <= rand.nextInt(5)) {
                        col.sections[sy].setBlock(lx, wy & 15, lz, (7 << 4));
                        continue;
                    }
                    uint16_t cur = col.sections[sy].getBlock(lx, wy & 15, lz);
                    if (cur == 0) {
                        rem = -1;
                        continue;
                    }

                    if (((cur >> 4) & 0xFF) == 1) {
                        if (rem == -1) {
                            if (depth <= 0) {
                                topBlk = 0;
                                filBlk = (1 << 4);
                            } else if (wy >= SEA_LEVEL - 4 && wy <= SEA_LEVEL + 1) {
                                topBlk = isDesert ? (12 << 4) : (2 << 4);
                                filBlk = isDesert ? (12 << 4) : (3 << 4);
                                if (gravelBeach) { topBlk = 0; filBlk = (13 << 4); }
                                else if (sandBeach) { topBlk = (12 << 4); filBlk = (12 << 4); }
                            }
                            if (wy < SEA_LEVEL && topBlk == 0) topBlk = (9 << 4);
                            rem = depth;

                            if (wy >= SEA_LEVEL - 1) {
                                col.sections[sy].setBlock(lx, wy & 15, lz, topBlk);
                            } else {
                                col.sections[sy].setBlock(lx, wy & 15, lz, filBlk);
                            }
                        } else if (rem > 0) {
                            rem--;
                            col.sections[sy].setBlock(lx, wy & 15, lz, filBlk);
                            if (rem == 0 && filBlk == (12 << 4)) {
                                rem = rand.nextInt(4);
                                filBlk = (24 << 4);
                            }
                        }
                    }
                }
            }
        }

        carveBetaCaves(col, cx, cz);
    }

    void carveBetaCaves(ChunkColumn& col, int32_t targetCx, int32_t targetCz) const {
        JavaRandomB173 wRand(seed);
        int64_t sX = wRand.nextLong();
        int64_t sZ = wRand.nextLong();
        constexpr int RANGE = 8;

        auto carveTunnel = [&](int64_t tunnelSeed, double x, double y, double z,
                               float width, float yaw, float pitch, int startStep, int maxSteps, double vertScale) {
            double centerX = static_cast<double>(targetCx * 16 + 8);
            double centerZ = static_cast<double>(targetCz * 16 + 8);
            float dYaw = 0.0f, dPitch = 0.0f;
            JavaRandomB173 rand(tunnelSeed);

            if (maxSteps <= 0) {
                int maxRangeBlocks = RANGE * 16 - 16;
                maxSteps = maxRangeBlocks - rand.nextInt(maxRangeBlocks / 4);
            }
            bool isRoom = false;
            if (startStep == -1) {
                startStep = maxSteps / 2;
                isRoom = true;
            }

            int branchStep = rand.nextInt(maxSteps / 2) + maxSteps / 4;
            bool steepPitch = (rand.nextInt(6) == 0);

            for (int step = startStep; step < maxSteps; ++step) {
                double radH = 1.5 + static_cast<double>(std::sin(static_cast<float>(step) * 3.14159265f / static_cast<float>(maxSteps)) * width * 1.0f);
                double radV = radH * vertScale;

                float cosP = std::cos(pitch);
                float sinP = std::sin(pitch);
                x += std::cos(yaw) * cosP;
                y += sinP;
                z += std::sin(yaw) * cosP;

                pitch *= steepPitch ? 0.92f : 0.70f;
                pitch += dPitch * 0.1f;
                yaw   += dYaw * 0.1f;
                dPitch *= 0.90f;
                dYaw   *= 0.75f;
                dPitch += (rand.nextFloat() - rand.nextFloat()) * rand.nextFloat() * 2.0f;
                dYaw   += (rand.nextFloat() - rand.nextFloat()) * rand.nextFloat() * 4.0f;

                if (!isRoom && step == branchStep && width > 1.0f) {
                    float w1 = rand.nextFloat() * 0.5f + 0.5f;
                    int64_t s1 = rand.nextLong();
                    float w2 = rand.nextFloat() * 0.5f + 0.5f;
                    int64_t s2 = rand.nextLong();

                    auto branchSub = [&](int64_t subSeed, float bWidth, float bYaw, float bPitch) {
                        double bx = x, by = y, bz = z;
                        float bdYaw = 0.0f, bdPitch = 0.0f;
                        JavaRandomB173 bRand(subSeed);
                        int bMax = maxSteps;
                        int bStart = step;
                        bool bSteep = (bRand.nextInt(6) == 0);
                        for (int bs = bStart; bs < bMax; ++bs) {
                            double brH = 1.5 + static_cast<double>(std::sin(static_cast<float>(bs) * 3.14159265f / static_cast<float>(bMax)) * bWidth);
                            double brV = brH * 1.0;
                            float bCosP = std::cos(bPitch);
                            bx += std::cos(bYaw) * bCosP;
                            by += std::sin(bPitch);
                            bz += std::sin(bYaw) * bCosP;
                            bPitch = bPitch * (bSteep ? 0.92f : 0.70f) + bdPitch * 0.1f;
                            bYaw  += bdYaw * 0.1f;
                            bdPitch = bdPitch * 0.90f + (bRand.nextFloat() - bRand.nextFloat()) * bRand.nextFloat() * 2.0f;
                            bdYaw   = bdYaw * 0.75f   + (bRand.nextFloat() - bRand.nextFloat()) * bRand.nextFloat() * 4.0f;

                            if (bRand.nextInt(4) == 0) continue;
                            double dcx = bx - centerX, dcz = bz - centerZ;
                            double rem = static_cast<double>(bMax - bs);
                            double maxD = static_cast<double>(bWidth + 2.0f + 16.0f);
                            if (dcx * dcx + dcz * dcz - rem * rem > maxD * maxD) return;
                            if (bx < centerX - 16.0 - brH * 2.0 || bz < centerZ - 16.0 - brH * 2.0 ||
                                bx > centerX + 16.0 + brH * 2.0 || bz > centerZ + 16.0 + brH * 2.0) continue;

                            int minX = std::max(0,   static_cast<int>(std::floor(bx - brH)) - targetCx * 16 - 1);
                            int maxX = std::min(16,  static_cast<int>(std::floor(bx + brH)) - targetCx * 16 + 1);
                            int minY = std::max(1,   static_cast<int>(std::floor(by - brV)) - 1);
                            int maxY = std::min(120, static_cast<int>(std::floor(by + brV)) + 1);
                            int minZ = std::max(0,   static_cast<int>(std::floor(bz - brH)) - targetCz * 16 - 1);
                            int maxZ = std::min(16,  static_cast<int>(std::floor(bz + brH)) - targetCz * 16 + 1);

                            bool hitWater = false;
                            for (int lx = minX; !hitWater && lx < maxX; ++lx) {
                                for (int lz = minZ; !hitWater && lz < maxZ; ++lz) {
                                    for (int wy = maxY + 1; !hitWater && wy >= minY - 1; --wy) {
                                        if (wy >= 0 && wy < 128) {
                                            uint8_t id = static_cast<uint8_t>((col.sections[wy >> 4].getBlock(lx, wy & 15, lz) >> 4) & 0xFF);
                                            if (id == 8 || id == 9) hitWater = true;
                                            if (wy != minY - 1 && lx != minX && lx != maxX - 1 && lz != minZ && lz != maxZ - 1) wy = minY;
                                        }
                                    }
                                }
                            }
                            if (hitWater) continue;

                            for (int lx = minX; lx < maxX; ++lx) {
                                double nx = (static_cast<double>(targetCx * 16 + lx) + 0.5 - bx) / brH;
                                for (int lz = minZ; lz < maxZ; ++lz) {
                                    double nz = (static_cast<double>(targetCz * 16 + lz) + 0.5 - bz) / brH;
                                    if (nx * nx + nz * nz >= 1.0) continue;
                                    bool hitGrass = false;
                                    for (int wy = maxY; wy > minY; --wy) {
                                        double ny = (static_cast<double>(wy - 1) + 0.5 - by) / brV;
                                        if (ny > -0.7 && nx * nx + ny * ny + nz * nz < 1.0) {
                                            uint8_t id = static_cast<uint8_t>((col.sections[(wy - 1) >> 4].getBlock(lx, (wy - 1) & 15, lz) >> 4) & 0xFF);
                                            if (id == 2) hitGrass = true;
                                            if (id == 1 || id == 3 || id == 2) {
                                                if (wy - 1 < 10) {
                                                    col.sections[(wy - 1) >> 4].setBlock(lx, (wy - 1) & 15, lz, (11 << 4));
                                                } else {
                                                    col.sections[(wy - 1) >> 4].setBlock(lx, (wy - 1) & 15, lz, 0);
                                                    if (hitGrass && wy - 2 >= 0 && ((col.sections[(wy - 2) >> 4].getBlock(lx, (wy - 2) & 15, lz) >> 4) & 0xFF) == 3) {
                                                        col.sections[(wy - 2) >> 4].setBlock(lx, (wy - 2) & 15, lz, (2 << 4));
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    };
                    branchSub(s1, w1, yaw - 1.5707964f, pitch / 3.0f);
                    branchSub(s2, w2, yaw + 1.5707964f, pitch / 3.0f);
                    return;
                }

                if (!isRoom && rand.nextInt(4) == 0) continue;

                double dcx = x - centerX;
                double dcz = z - centerZ;
                double rem = static_cast<double>(maxSteps - step);
                double maxD = static_cast<double>(width + 2.0f + 16.0f);
                if (dcx * dcx + dcz * dcz - rem * rem > maxD * maxD) return;

                if (x < centerX - 16.0 - radH * 2.0 || z < centerZ - 16.0 - radH * 2.0 ||
                    x > centerX + 16.0 + radH * 2.0 || z > centerZ + 16.0 + radH * 2.0) {
                    if (isRoom) break;
                    continue;
                }

                int minX = std::max(0,   static_cast<int>(std::floor(x - radH)) - targetCx * 16 - 1);
                int maxX = std::min(16,  static_cast<int>(std::floor(x + radH)) - targetCx * 16 + 1);
                int minY = std::max(1,   static_cast<int>(std::floor(y - radV)) - 1);
                int maxY = std::min(120, static_cast<int>(std::floor(y + radV)) + 1);
                int minZ = std::max(0,   static_cast<int>(std::floor(z - radH)) - targetCz * 16 - 1);
                int maxZ = std::min(16,  static_cast<int>(std::floor(z + radH)) - targetCz * 16 + 1);

                bool hitWater = false;
                for (int lx = minX; !hitWater && lx < maxX; ++lx) {
                    for (int lz = minZ; !hitWater && lz < maxZ; ++lz) {
                        for (int wy = maxY + 1; !hitWater && wy >= minY - 1; --wy) {
                            if (wy >= 0 && wy < 128) {
                                uint8_t id = static_cast<uint8_t>((col.sections[wy >> 4].getBlock(lx, wy & 15, lz) >> 4) & 0xFF);
                                if (id == 8 || id == 9) hitWater = true;
                                if (wy != minY - 1 && lx != minX && lx != maxX - 1 && lz != minZ && lz != maxZ - 1) wy = minY;
                            }
                        }
                    }
                }
                if (!hitWater) {
                    for (int lx = minX; lx < maxX; ++lx) {
                        double nx = (static_cast<double>(targetCx * 16 + lx) + 0.5 - x) / radH;
                        for (int lz = minZ; lz < maxZ; ++lz) {
                            double nz = (static_cast<double>(targetCz * 16 + lz) + 0.5 - z) / radH;
                            if (nx * nx + nz * nz >= 1.0) continue;
                            bool hitGrass = false;
                            for (int wy = maxY; wy > minY; --wy) {
                                double ny = (static_cast<double>(wy - 1) + 0.5 - y) / radV;
                                if (ny > -0.7 && nx * nx + ny * ny + nz * nz < 1.0) {
                                    uint8_t id = static_cast<uint8_t>((col.sections[(wy - 1) >> 4].getBlock(lx, (wy - 1) & 15, lz) >> 4) & 0xFF);
                                    if (id == 2) hitGrass = true;
                                    if (id == 1 || id == 3 || id == 2) {
                                        if (wy - 1 < 10) {
                                            col.sections[(wy - 1) >> 4].setBlock(lx, (wy - 1) & 15, lz, (11 << 4));
                                        } else {
                                            col.sections[(wy - 1) >> 4].setBlock(lx, (wy - 1) & 15, lz, 0);
                                            if (hitGrass && wy - 2 >= 0 && ((col.sections[(wy - 2) >> 4].getBlock(lx, (wy - 2) & 15, lz) >> 4) & 0xFF) == 3) {
                                                col.sections[(wy - 2) >> 4].setBlock(lx, (wy - 2) & 15, lz, (2 << 4));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (isRoom) break;
            }
        };

        for (int ocx = targetCx - RANGE; ocx <= targetCx + RANGE; ++ocx) {
            for (int ocz = targetCz - RANGE; ocz <= targetCz + RANGE; ++ocz) {
                JavaRandomB173 rand((static_cast<int64_t>(ocx) * sX) ^ (static_cast<int64_t>(ocz) * sZ) ^ seed);
                int count = rand.nextInt(rand.nextInt(rand.nextInt(40) + 1) + 1);
                if (rand.nextInt(15) != 0) count = 0;

                for (int c = 0; c < count; ++c) {
                    double x = static_cast<double>(ocx * 16 + rand.nextInt(16));
                    double y = static_cast<double>(rand.nextInt(rand.nextInt(120) + 8));
                    double z = static_cast<double>(ocz * 16 + rand.nextInt(16));
                    int tunnels = 1;
                    if (rand.nextInt(4) == 0) {
                        int64_t roomSeed = rand.nextLong();
                        float roomW = 1.0f + rand.nextFloat() * 6.0f;
                        carveTunnel(roomSeed, x, y, z, roomW, 0.0f, 0.0f, -1, -1, 0.5);
                        tunnels += rand.nextInt(4);
                    }
                    for (int t = 0; t < tunnels; ++t) {
                        float yaw   = rand.nextFloat() * 3.1415927f * 2.0f;
                        float pitch = ((rand.nextFloat() - 0.5f) * 2.0f) / 8.0f;
                        float width = rand.nextFloat() * 2.0f + rand.nextFloat();
                        carveTunnel(rand.nextLong(), x, y, z, width, yaw, pitch, 0, 0, 1.0);
                    }
                }
            }
        }
    }

    void generateNetherBase(ChunkColumn& col, int32_t cx, int32_t cz, BetaWorkerScratchpad& pad) const {
        col.sectionMask = 0xFF;
        for (int s = 0; s < SECTIONS_PER_COL; ++s) col.sections[s].clear(0);
        JavaRandomB173 rand(static_cast<int64_t>(cx) * 341873128712LL + static_cast<int64_t>(cz) * 132897987541LL);

        int baseX = cx * 4, baseZ = cz * 4;
        double yOffsets[17];
        for (int y = 0; y < 17; ++y) {
            yOffsets[y] = std::cos(static_cast<double>(y) * 3.14159265 * 6.0 / 17.0) * 2.0;
            double edge = static_cast<double>(y);
            if (y > 8) edge = static_cast<double>(16 - y);
            if (edge < 4.0) {
                edge = 4.0 - edge;
                yOffsets[y] -= edge * edge * edge * 10.0;
            }
        }

        for (int x = 0; x < 5; ++x) {
            for (int z = 0; z < 5; ++z) {
                int gx = baseX + x, gz = baseZ + z;
                for (int y = 0; y < 17; ++y) {
                    double minV  = minLimitPerlin.sample3D(gx, y, gz, 684.412, 2053.236, 684.412) / 512.0;
                    double maxV  = maxLimitPerlin.sample3D(gx, y, gz, 684.412, 2053.236, 684.412) / 512.0;
                    double mainV = (mainPerlin.sample3D(gx, y, gz, 684.412 / 80.0, 2053.236 / 60.0, 684.412 / 80.0) / 10.0 + 1.0) * 0.5;
                    double dens  = (mainV < 0.0) ? minV : ((mainV > 1.0) ? maxV : (minV + (maxV - minV) * mainV));
                    dens -= yOffsets[y];
                    if (y > 13) {
                        double slide = static_cast<double>(y - 13) / 3.0;
                        dens = dens * (1.0 - slide) + (-10.0 * slide);
                    }
                    pad.densityField[(x * 5 + z) * 17 + y] = dens;
                }
            }
        }

        for (int cxCell = 0; cxCell < 4; ++cxCell) {
            for (int czCell = 0; czCell < 4; ++czCell) {
                for (int cyCell = 0; cyCell < 16; ++cyCell) {
                    double d000 = pad.densityField[((cxCell + 0) * 5 + (czCell + 0)) * 17 + (cyCell + 0)];
                    double d001 = pad.densityField[((cxCell + 0) * 5 + (czCell + 1)) * 17 + (cyCell + 0)];
                    double d100 = pad.densityField[((cxCell + 1) * 5 + (czCell + 0)) * 17 + (cyCell + 0)];
                    double d101 = pad.densityField[((cxCell + 1) * 5 + (czCell + 1)) * 17 + (cyCell + 0)];

                    double d010 = (pad.densityField[((cxCell + 0) * 5 + (czCell + 0)) * 17 + (cyCell + 1)] - d000) * 0.125;
                    double d011 = (pad.densityField[((cxCell + 0) * 5 + (czCell + 1)) * 17 + (cyCell + 1)] - d001) * 0.125;
                    double d110 = (pad.densityField[((cxCell + 1) * 5 + (czCell + 0)) * 17 + (cyCell + 1)] - d100) * 0.125;
                    double d111 = (pad.densityField[((cxCell + 1) * 5 + (czCell + 1)) * 17 + (cyCell + 1)] - d101) * 0.125;

                    for (int sy = 0; sy < 8; ++sy) {
                        int wy = cyCell * 8 + sy;
                        double v00 = d000, v01 = d001;
                        double v10 = (d100 - d000) * 0.25, v11 = (d101 - d001) * 0.25;

                        for (int sx = 0; sx < 4; ++sx) {
                            int lx = cxCell * 4 + sx;
                            double dens = v00, stepZ = (v01 - v00) * 0.25;
                            for (int sz = 0; sz < 4; ++sz) {
                                int lz = czCell * 4 + sz;
                                if (dens > 0.0) {
                                    col.sections[wy >> 4].setBlock(lx, wy & 15, lz, (87 << 4));
                                } else if (wy < 32) {
                                    col.sections[wy >> 4].setBlock(lx, wy & 15, lz, (11 << 4));
                                }
                                dens += stepZ;
                            }
                            v00 += v10;
                            v01 += v11;
                        }
                        d000 += d010; d001 += d011; d100 += d110; d101 += d111;
                    }
                }
            }
        }

        for (int lx = 0; lx < 16; ++lx) {
            for (int lz = 0; lz < 16; ++lz) {
                int wx = cx * 16 + lx, wz = cz * 16 + lz;
                bool soulBeach   = (beachNoise.sample2D(wx, wz, 0.03125, 0.03125) + rand.nextDouble() * 0.2) > 0.0;
                bool gravelBeach = (beachNoise.sample2D(wz, wx, 0.03125, 0.03125) + rand.nextDouble() * 0.2) > 0.0;
                int depth = static_cast<int>(surfaceNoise.sample2D(wx, wz, 0.0625, 0.0625) / 3.0 + 3.0 + rand.nextDouble() * 0.25);

                int rem = -1;
                uint16_t topBlk = (87 << 4), filBlk = (87 << 4);
                for (int wy = 127; wy >= 0; --wy) {
                    int sy = wy >> 4;
                    if (wy >= 127 - rand.nextInt(5) || wy <= rand.nextInt(5)) {
                        col.sections[sy].setBlock(lx, wy & 15, lz, (7 << 4));
                        continue;
                    }
                    uint16_t cur = col.sections[sy].getBlock(lx, wy & 15, lz);
                    if (cur == 0) { rem = -1; continue; }

                    if (((cur >> 4) & 0xFF) == 87) {
                        if (rem == -1) {
                            topBlk = (87 << 4); filBlk = (87 << 4);
                            if (wy >= 30 && wy <= 35) {
                                if (soulBeach)   { topBlk = (88 << 4); filBlk = (88 << 4); }
                                if (gravelBeach) { topBlk = (13 << 4); filBlk = (87 << 4); }
                            }
                            if (wy < 32 && topBlk == 0) topBlk = (11 << 4);
                            rem = depth;
                            col.sections[sy].setBlock(lx, wy & 15, lz, (wy >= 31) ? topBlk : filBlk);
                        } else if (rem > 0) {
                            rem--;
                            col.sections[sy].setBlock(lx, wy & 15, lz, filBlk);
                        }
                    }
                }
            }
        }

        for (int g = 0; g < 10; ++g) {
            int gx = rand.nextInt(16), gy = 4 + rand.nextInt(120), gz = rand.nextInt(16);
            if (((col.sections[gy >> 4].getBlock(gx, gy & 15, gz) >> 4) & 0xFF) == 87 &&
                col.sections[(gy - 1) >> 4].getBlock(gx, (gy - 1) & 15, gz) == 0) {
                for (int b = 0; b < 32; ++b) {
                    int bx = std::clamp(gx + rand.nextInt(5) - 2, 0, 15);
                    int by = std::clamp(gy - rand.nextInt(7), 2, 125);
                    int bz = std::clamp(gz + rand.nextInt(5) - 2, 0, 15);
                    if (col.sections[by >> 4].getBlock(bx, by & 15, bz) == 0) {
                        col.sections[by >> 4].setBlock(bx, by & 15, bz, (89 << 4));
                    }
                }
            }
        }
        for (int f = 0; f < 10; ++f) {
            int fx = rand.nextInt(16), fy = 32 + rand.nextInt(80), fz = rand.nextInt(16);
            if (col.sections[fy >> 4].getBlock(fx, fy & 15, fz) == 0 &&
                ((col.sections[(fy - 1) >> 4].getBlock(fx, (fy - 1) & 15, fz) >> 4) & 0xFF) == 87) {
                col.sections[fy >> 4].setBlock(fx, fy & 15, fz, (51 << 4));
            }
        }
    }

    void decorateChunk(WorldMap& world, ChunkColumn& col, int32_t cx, int32_t cz, BetaWorkerScratchpad& pad) const {
        (void)col;
        int k = cx * 16;
        int l = cz * 16;

        float centerTemp = 0.5f, centerRain = 0.5f;
        sampleClimateAt(static_cast<double>(k + 16), static_cast<double>(l + 16), centerTemp, centerRain);
        BetaBiomeId centerBiome = getBiomeFromClimate(centerTemp, centerRain);

        JavaRandomB173 rand(seed);
        int64_t l1 = (rand.nextLong() / 2LL) * 2LL + 1LL;
        int64_t l2 = (rand.nextLong() / 2LL) * 2LL + 1LL;
        rand.setSeed(((static_cast<int64_t>(cx) * l1 + static_cast<int64_t>(cz) * l2) ^ seed));

        // 1. Water Lakes
        if (centerBiome != BIOME_DESERT && centerBiome != BIOME_ICE_DESERT && rand.nextInt(4) == 0) {
            int x = k + rand.nextInt(16) + 8;
            int y = rand.nextInt(128);
            int z = l + rand.nextInt(16) + 8;
            WorldFeatures::generateWorldLake(world, rand, x, y, z, (9 << 4), pad.lakeGrid);
        }

        // 2. Lava Lakes
        if (rand.nextInt(8) == 0) {
            int x = k + rand.nextInt(16) + 8;
            int y = rand.nextInt(rand.nextInt(120) + 8);
            int z = l + rand.nextInt(16) + 8;
            if (y < 64 || rand.nextInt(10) == 0) {
                WorldFeatures::generateWorldLake(world, rand, x, y, z, (11 << 4), pad.lakeGrid);
            }
        }

        // 3. Dungeons
        for (int d = 0; d < 8; ++d) {
            int x = k + rand.nextInt(16) + 8;
            int y = rand.nextInt(128);
            int z = l + rand.nextInt(16) + 8;
            WorldFeatures::generateDungeon(world, rand, x, y, z);
        }

        // 4. Clay Disks
        for (int c = 0; c < 10; ++c) {
            int x = k + rand.nextInt(16);
            int y = rand.nextInt(128);
            int z = l + rand.nextInt(16);
            WorldFeatures::generateClayPatch(world, rand, x, y, z, 32);
        }

        // 5. Mineral Ore Veins
        WorldFeatures::generateVein(world, rand, k, l, (3 << 4),  32, 20, 0, 128); // Dirt
        WorldFeatures::generateVein(world, rand, k, l, (13 << 4), 32, 10, 0, 128); // Gravel
        WorldFeatures::generateVein(world, rand, k, l, (16 << 4), 16, 20, 0, 128); // Coal
        WorldFeatures::generateVein(world, rand, k, l, (15 << 4), 8,  20, 0, 64);  // Iron
        WorldFeatures::generateVein(world, rand, k, l, (14 << 4), 8,  2,  0, 32);  // Gold
        WorldFeatures::generateVein(world, rand, k, l, (73 << 4), 7,  8,  0, 16);  // Redstone
        WorldFeatures::generateVein(world, rand, k, l, (56 << 4), 7,  1,  0, 16);  // Diamond
        for (int lp = 0; lp < 1; ++lp) {                                           // Lapis Lazuli
            int by = rand.nextInt(16) + rand.nextInt(16);
            WorldFeatures::generateVein(world, rand, k, l, (21 << 4), 6, 1, by, by + 1);
        }

        // 6. Dendrological Trees
        double treeNoise = (forestNoise.sample2D(static_cast<double>(k) * 0.5, static_cast<double>(l) * 0.5, 1.0, 1.0) / 8.0 +
                            rand.nextDouble() * 4.0 + 4.0) / 3.0;
        int treeCount = 0;
        if (rand.nextInt(10) == 0) treeCount++;

        if (centerBiome == BIOME_FOREST)               treeCount += static_cast<int>(treeNoise) + 5;
        else if (centerBiome == BIOME_RAINFOREST)      treeCount += static_cast<int>(treeNoise) + 5;
        else if (centerBiome == BIOME_SEASONAL_FOREST) treeCount += static_cast<int>(treeNoise) + 2;
        else if (centerBiome == BIOME_TAIGA)           treeCount += static_cast<int>(treeNoise) + 5;
        else if (centerBiome == BIOME_DESERT || centerBiome == BIOME_TUNDRA || centerBiome == BIOME_PLAINS) treeCount -= 20;

        for (int t = 0; t < treeCount; ++t) {
            int tx = k + rand.nextInt(16) + 8;
            int tz = l + rand.nextInt(16) + 8;
            int ty = WorldFeatures::getSurfaceSolidY(world, tx, tz);
            if (ty <= 1 || ty >= 120) continue;

            if (centerBiome == BIOME_TAIGA) {
                if (rand.nextInt(3) == 0) WorldFeatures::generateTaigaTree1(world, rand, tx, ty, tz);
                else                      WorldFeatures::generateTaigaTree2(world, rand, tx, ty, tz);
            } else if (centerBiome == BIOME_FOREST && rand.nextInt(5) == 0) {
                WorldFeatures::generateStandardTree(world, rand, tx, ty, tz, 2); // Birch
            } else if (rand.nextInt(10) == 0) {
                WorldFeatures::generateBigOakTree(world, rand, tx, ty, tz);      // Big Oak
            } else {
                WorldFeatures::generateStandardTree(world, rand, tx, ty, tz, 0); // Standard Oak
            }
        }

        // 7. Botanical Flora & Mushrooms
        int flowerCount = (centerBiome == BIOME_FOREST || centerBiome == BIOME_PLAINS) ? 3 : 2;
        for (int f = 0; f < flowerCount; ++f) {
            int fx = k + rand.nextInt(16) + 8;
            int fy = rand.nextInt(128);
            int fz = l + rand.nextInt(16) + 8;
            WorldFeatures::generateFlowerPatch(world, rand, fx, fy, fz, (37 << 4));
        }

        int grassCount = (centerBiome == BIOME_PLAINS) ? 10 : ((centerBiome == BIOME_FOREST || centerBiome == BIOME_RAINFOREST) ? 2 : 1);
        for (int g = 0; g < grassCount; ++g) {
            uint16_t grassMeta = (centerBiome == BIOME_RAINFOREST && rand.nextInt(3) != 0) ? 2 : 1;
            int gx = k + rand.nextInt(16) + 8;
            int gy = rand.nextInt(128);
            int gz = l + rand.nextInt(16) + 8;
            WorldFeatures::generateTallGrassPatch(world, rand, gx, gy, gz, static_cast<uint16_t>((31 << 4) | grassMeta));
        }

        if (centerBiome == BIOME_DESERT) {
            for (int db = 0; db < 2; ++db) {
                int dx = k + rand.nextInt(16) + 8;
                int dy = rand.nextInt(128);
                int dz = l + rand.nextInt(16) + 8;
                WorldFeatures::generateDeadBushPatch(world, rand, dx, dy, dz);
            }
        }

        if (rand.nextInt(2) == 0) {
            int rx = k + rand.nextInt(16) + 8;
            int ry = rand.nextInt(128);
            int rz = l + rand.nextInt(16) + 8;
            WorldFeatures::generateFlowerPatch(world, rand, rx, ry, rz, (38 << 4));
        }
        if (rand.nextInt(4) == 0) {
            int mx = k + rand.nextInt(16) + 8;
            int my = rand.nextInt(128);
            int mz = l + rand.nextInt(16) + 8;
            WorldFeatures::generateMushroomPatch(world, rand, mx, my, mz, (39 << 4));
        }
        if (rand.nextInt(8) == 0) {
            int mx = k + rand.nextInt(16) + 8;
            int my = rand.nextInt(128);
            int mz = l + rand.nextInt(16) + 8;
            WorldFeatures::generateMushroomPatch(world, rand, mx, my, mz, (40 << 4));
        }

        // 8. Sugar Canes (Fixed: Enforces canBlockStay on every stalk segment, eliminating floating reeds)
        WorldFeatures::generateSugarCanePatch(world, rand, k, l);

        // 9. Cucurbita Pumpkins
        WorldFeatures::generatePumpkinPatch(world, rand, k, l);

        // 10. Cacti (Fixed: Enforces 4-way horizontal perimeter clearance)
        int cactusAttempts = (centerBiome == BIOME_DESERT) ? 10 : 1;
        WorldFeatures::generateCactusPatch(world, rand, k, l, cactusAttempts);

        // 11. Liquid Spring Wells
        WorldFeatures::generateLiquidSprings(world, rand, k, l);

        // 12. Cryospheric Freezing & Snow Layer Pass (Fixed: canSupportSnow ensures snow NEVER rests on Cacti)
        WorldFeatures::generateSnowAndIcePass(world, k, l, col.temperature);
    }

    template<typename QueueType>
    void tryPromoteAndDecorate(WorldMap& world, int32_t cx, int32_t cz, BetaWorkerScratchpad& pad, QueueType& meshQueue) {
        {
            std::lock_guard<std::mutex> lock(world.getChunkLock(cx, cz));
            ChunkColumn& col = world.getSlot(cx, cz);
            if (col.chunkX != cx || col.chunkZ != cz) return;
            if (col.state.load(std::memory_order_acquire) != CHUNK_TERRAIN) return;

            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0) continue;
                    if (!world.getTerrainOrReadyChunk(cx + dx, cz + dz)) return;
                }
            }
            col.state.store(CHUNK_DECORATING, std::memory_order_release);
        }

        ChunkColumn& col = world.getSlot(cx, cz);
        if (world.dimension == 0) {
            decorateChunk(world, col, cx, cz, pad);
        }
        world.computeColumnInitialLighting(col, cx, cz);
        col.dirty = true;
        col.state.store(CHUNK_READY, std::memory_order_release);

        notifyNeighborsReady(world, cx, cz, meshQueue);
    }

    template<typename QueueType>
    static void notifyNeighborsReady(WorldMap& world, int32_t cx, int32_t cz, QueueType& meshQueue) {
        meshQueue.push({cx, cz});
        static constexpr int32_t dirs[4][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
        for (const auto& d : dirs) {
            if (world.getReadyChunk(cx + d[0], cz + d[1])) {
                meshQueue.push({cx + d[0], cz + d[1]});
            }
        }
    }
};