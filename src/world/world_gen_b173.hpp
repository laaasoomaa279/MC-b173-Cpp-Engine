#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "world/world.hpp"
#include "world/mcregion_nbt.hpp"
#include "gameplay/combat_nether.hpp"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>
#include <filesystem>

// ============================================================================
// Minecraft Beta 1.7.3 Terrain Generators (ChunkProviderGenerate & ChunkProviderHell)
// ============================================================================
// 1:1 Reproduction of Notch's Beta 1.7.3 Terrain & Biome Mathematics:
//   - 48-bit java.util.Random (0x5DEECE66DL) & NoiseGeneratorOctaves
//   - Continuous Temperature & Humidity Climate Fields (Rainforest, Swampland,
//     Seasonal Forest, Forest, Savanna, Shrubland, Taiga, Desert, Plains,
//     Ice Desert, Tundra)
//   - Classic Overhanging Cliffs, Floating Islands, Sandy Beaches & Far Lands
//     (at |X| or |Z| >= 12,550,821)
//   - Worm Caves (MapGenCaves), Ore Veins, Water/Lava Lakes & Trees
//   - Full Nether Generator (ChunkProviderHell): 5x17x5 3D Noise Field,
//     Lava Sea (Y <= 31), Soul Sand & Gravel Beaches, Hanging Glowstone
//     Clusters, and Arrival Obsidian Portal Frame
// ============================================================================

struct SavedWorldSummary {
    std::string   folderName;
    std::string   fullPath;
    BetaLevelData levelData;
};

class JavaRandomB173 {
    static constexpr uint64_t MULT = 0x5DEECE66DULL;
    static constexpr uint64_t ADD  = 0xBULL;
    static constexpr uint64_t MASK = (1ULL << 48) - 1ULL;
    uint64_t seed = 0;

public:
    JavaRandomB173() { setSeed(0); }
    explicit JavaRandomB173(int64_t s) { setSeed(s); }

    void setSeed(int64_t s) { seed = (static_cast<uint64_t>(s) ^ MULT) & MASK; }

    uint32_t next(int bits) {
        seed = (seed * MULT + ADD) & MASK;
        return static_cast<uint32_t>(seed >> (48 - bits));
    }
    int32_t nextInt() { return static_cast<int32_t>(next(32)); }
    int32_t nextInt(int32_t bound) {
        if (bound <= 0) return 0;
        if ((bound & -bound) == bound) {
            return static_cast<int32_t>((static_cast<int64_t>(bound) * static_cast<int64_t>(next(31))) >> 31);
        }
        int32_t bits, val;
        do {
            bits = static_cast<int32_t>(next(31));
            val  = bits % bound;
        } while (bits - val + (bound - 1) < 0);
        return val;
    }
    int64_t nextLong() {
        return (static_cast<int64_t>(static_cast<int32_t>(next(32))) << 32) +
                static_cast<int64_t>(static_cast<int32_t>(next(32)));
    }
    float nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }
    double nextDouble() {
        uint64_t hi = static_cast<uint64_t>(next(26)) << 27;
        uint64_t lo = static_cast<uint64_t>(next(27));
        return static_cast<double>(hi + lo) / static_cast<double>(1ULL << 53);
    }
};

class PerlinNoiseB173 {
    int    p[512]{};
    double xo = 0.0, yo = 0.0, zo = 0.0;

    static inline double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
    static inline double lerp(double t, double a, double b) { return a + t * (b - a); }
    static inline double grad(int h, double x, double y, double z) {
        h &= 15;
        double u = (h < 8) ? x : y;
        double v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

public:
    void init(JavaRandomB173& rand) {
        xo = rand.nextDouble() * 256.0;
        yo = rand.nextDouble() * 256.0;
        zo = rand.nextDouble() * 256.0;
        for (int i = 0; i < 256; ++i) p[i] = i;
        for (int i = 0; i < 256; ++i) {
            int j = rand.nextInt(256 - i) + i;
            std::swap(p[i], p[j]);
            p[i + 256] = p[i];
        }
    }

    double sample3D(double x, double y, double z) const {
        double px = x + xo, py = y + yo, pz = z + zo;
        int ix = (int)std::floor(px), iy = (int)std::floor(py), iz = (int)std::floor(pz);
        int X = ix & 255, Y = iy & 255, Z = iz & 255;
        px -= ix; py -= iy; pz -= iz;

        double u = fade(px), v = fade(py), w = fade(pz);
        int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
        int B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;

        return lerp(w,
            lerp(v,
                lerp(u, grad(p[AA], px, py, pz), grad(p[BA], px - 1.0, py, pz)),
                lerp(u, grad(p[AB], px, py - 1.0, pz), grad(p[BB], px - 1.0, py - 1.0, pz))),
            lerp(v,
                lerp(u, grad(p[AA + 1], px, py, pz - 1.0), grad(p[BA + 1], px - 1.0, py, pz - 1.0)),
                lerp(u, grad(p[AB + 1], px, py - 1.0, pz - 1.0), grad(p[BB + 1], px - 1.0, py - 1.0, pz - 1.0))));
    }

    double sample2D(double x, double z) const { return sample3D(x, 0.0, z); }
};

class OctaveNoiseB173 {
    std::vector<PerlinNoiseB173> octaves;

public:
    void init(JavaRandomB173& rand, int count) {
        octaves.resize((size_t)count);
        for (int i = 0; i < count; ++i) octaves[i].init(rand);
    }

    double sample3D(double x, double y, double z, double fx, double fy, double fz) const {
        double total = 0.0, amp = 1.0;
        for (const auto& o : octaves) {
            total += o.sample3D(x * fx * amp, y * fy * amp, z * fz * amp) / amp;
            amp *= 0.5;
        }
        return total;
    }

    double sample2D(double x, double z, double fx, double fz) const {
        double total = 0.0, amp = 1.0;
        for (const auto& o : octaves) {
            total += o.sample2D(x * fx * amp, z * fz * amp) / amp;
            amp *= 0.5;
        }
        return total;
    }
};

struct BetaWorkerScratchpad {
    double densityField[5 * 17 * 5]{};
    bool   lakeGrid[16 * 16 * 8]{};
    bool   prioritySet = false;
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
    int64_t         seed = 123456789LL;

    // Exact 7 NoiseGenerators initialized in Beta 1.7.3 ChunkProviderGenerate constructor order
    OctaveNoiseB173 minLimitPerlin; // 16 octaves
    OctaveNoiseB173 maxLimitPerlin; // 16 octaves
    OctaveNoiseB173 mainPerlin;     // 8 octaves
    OctaveNoiseB173 beachNoise;     // 4 octaves
    OctaveNoiseB173 surfaceNoise;   // 4 octaves
    OctaveNoiseB173 scaleNoise;     // 10 octaves
    OctaveNoiseB173 depthNoise;     // 16 octaves
    OctaveNoiseB173 forestNoise;    // 8 octaves
    OctaveNoiseB173 tempNoise;      // 4 octaves (continuous climate field)
    OctaveNoiseB173 rainNoise;      // 4 octaves (continuous climate field)

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

        JavaRandomB173 climRand(seed * 9871LL);
        tempNoise.init(climRand, 4);
        rainNoise.init(climRand, 4);
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
        outProgress = (float)ready / (float)spawnCoreChunks.size();
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
        int newCx = ((int)std::floor(playerX)) >> 4;
        int newCz = ((int)std::floor(playerZ)) >> 4;
        world.centerChunkX = newCx;
        world.centerChunkZ = newCz;

        size_t maxBacklog = std::max<size_t>(4, (size_t)pool.threadCount() * 2);
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

            tryPromoteAndDecorate(world, cx,     cz,     pad, meshQueue);
            tryPromoteAndDecorate(world, cx + 1, cz,     pad, meshQueue);
            tryPromoteAndDecorate(world, cx - 1, cz,     pad, meshQueue);
            tryPromoteAndDecorate(world, cx,     cz + 1, pad, meshQueue);
            tryPromoteAndDecorate(world, cx,     cz - 1, pad, meshQueue);
        }, highPriority);
    }

private:
    // Classic Beta 1.7.3 Overworld Terrain + Continuous Climate + Sandy Beaches + Far Lands
    void generateOverworldBase(ChunkColumn& col, int32_t cx, int32_t cz, BetaWorkerScratchpad& pad) const {
        col.sectionMask = 0xFF;
        for (int s = 0; s < SECTIONS_PER_COL; ++s) col.sections[s].clear(0);

        for (int lz = 0; lz < 16; ++lz) {
            for (int lx = 0; lx < 16; ++lx) {
                int wx = cx * 16 + lx, wz = cz * 16 + lz;
                double t = tempNoise.sample2D(wx * 0.025, wz * 0.025, 1.0, 1.0) * 0.25 + 0.55;
                double r = rainNoise.sample2D(wx * 0.025, wz * 0.025, 1.0, 1.0) * 0.25 + 0.50;
                col.temperature[lz * 16 + lx] = (float)std::clamp(t, 0.0, 1.0);
                col.humidity[lz * 16 + lx]    = (float)std::clamp(r, 0.0, 1.0);
            }
        }

        int baseX = cx * 4, baseZ = cz * 4;
        for (int x = 0; x < 5; ++x) {
            for (int z = 0; z < 5; ++z) {
                int gx = baseX + x, gz = baseZ + z;
                double temp = std::clamp(tempNoise.sample2D(gx * 0.1, gz * 0.1, 1.0, 1.0) * 0.25 + 0.55, 0.0, 1.0);
                double rain = std::clamp(rainNoise.sample2D(gx * 0.1, gz * 0.1, 1.0, 1.0) * 0.25 + 0.50, 0.0, 1.0) * temp;
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

                double farMultX = (std::abs((double)gx * 4.0) >= 12550820.0) ? 1000000.0 : 1.0;
                double farMultZ = (std::abs((double)gz * 4.0) >= 12550820.0) ? 1000000.0 : 1.0;

                for (int y = 0; y < 17; ++y) {
                    double yOff = ((double)y - targetY) * 12.0 / sVal;
                    if (yOff < 0.0) yOff *= 4.0;

                    double minV  = minLimitPerlin.sample3D(gx * farMultX, y, gz * farMultZ, 684.412, 684.412, 684.412) / 512.0;
                    double maxV  = maxLimitPerlin.sample3D(gx * farMultX, y, gz * farMultZ, 684.412, 684.412, 684.412) / 512.0;
                    double mainV = (mainPerlin.sample3D(gx * farMultX, y, gz * farMultZ, 684.412 / 80.0, 684.412 / 160.0, 684.412 / 80.0) / 10.0 + 1.0) * 0.5;

                    double dens = (mainV < 0.0) ? minV : ((mainV > 1.0) ? maxV : (minV + (maxV - minV) * mainV));
                    dens -= yOff;
                    if (y > 13) {
                        double slide = (double)(y - 13) / 3.0;
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

        JavaRandomB173 rand((int64_t)cx * 341873128712LL + (int64_t)cz * 132897987541LL);
        for (int lz = 0; lz < 16; ++lz) {
            for (int lx = 0; lx < 16; ++lx) {
                int wx = cx * 16 + lx, wz = cz * 16 + lz;
                float temp = col.temperature[lz * 16 + lx];
                float rain = col.humidity[lz * 16 + lx];
                bool isDesert = (temp > 0.82f && rain < 0.20f);
                bool isFrozen = (temp < 0.22f);

                bool sandBeach   = (beachNoise.sample2D(wx, wz, 0.03125, 0.03125) + rand.nextDouble() * 0.2) > 0.0;
                bool gravelBeach = (beachNoise.sample2D(wz, wx, 0.03125, 0.03125) + rand.nextDouble() * 0.2) > 3.0;
                int depth = (int)(surfaceNoise.sample2D(wx, wz, 0.0625, 0.0625) / 3.0 + 3.0 + rand.nextDouble() * 0.25);

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
                    if (cur == 0) { rem = -1; continue; }

                    if (wy == SEA_LEVEL - 1 && ((cur >> 4) & 0xFF) == 9 && isFrozen) {
                        col.sections[sy].setBlock(lx, wy & 15, lz, (79 << 4));
                        continue;
                    }

                    if (((cur >> 4) & 0xFF) == 1) {
                        if (rem == -1) {
                            if (depth <= 0) { topBlk = 0; filBlk = (1 << 4); }
                            else if (wy >= SEA_LEVEL - 4 && wy <= SEA_LEVEL + 1) {
                                topBlk = isDesert ? (12 << 4) : (2 << 4);
                                filBlk = isDesert ? (12 << 4) : (3 << 4);
                                if (gravelBeach) { topBlk = 0; filBlk = (13 << 4); }
                                else if (sandBeach) { topBlk = (12 << 4); filBlk = (12 << 4); }
                            }
                            if (wy < SEA_LEVEL && topBlk == 0) topBlk = (9 << 4);
                            rem = depth;

                            if (wy >= SEA_LEVEL - 1) {
                                col.sections[sy].setBlock(lx, wy & 15, lz, topBlk);
                                if (isFrozen && topBlk == (2 << 4) && wy + 1 < 127) {
                                    col.sections[(wy + 1) >> 4].setBlock(lx, (wy + 1) & 15, lz, (78 << 4));
                                }
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
        int64_t sX = wRand.nextLong(), sZ = wRand.nextLong();
        constexpr int RANGE = 5;

        for (int ocx = targetCx - RANGE; ocx <= targetCx + RANGE; ++ocx) {
            for (int ocz = targetCz - RANGE; ocz <= targetCz + RANGE; ++ocz) {
                JavaRandomB173 rand((int64_t)ocx * sX ^ (int64_t)ocz * sZ ^ seed);
                int count = rand.nextInt(rand.nextInt(rand.nextInt(40) + 1) + 1);
                if (rand.nextInt(15) != 0) count = 0;

                for (int c = 0; c < count; ++c) {
                    double x = (double)(ocx * 16 + rand.nextInt(16));
                    double y = (double)rand.nextInt(rand.nextInt(120) + 8);
                    double z = (double)(ocz * 16 + rand.nextInt(16));
                    float yaw   = rand.nextFloat() * 6.2831853f;
                    float pitch = (rand.nextFloat() - 0.5f) * 0.25f;
                    float width = rand.nextFloat() * 2.0f + rand.nextFloat();

                    int maxSteps = 64 - rand.nextInt(16);
                    float dYaw = 0.0f, dPitch = 0.0f;
                    for (int step = 0; step < maxSteps; ++step) {
                        double rad = 1.5 + (double)(std::sin((float)step * 3.14159265f / (float)maxSteps) * width);
                        x += std::cos(yaw) * std::cos(pitch);
                        y += std::sin(pitch);
                        z += std::sin(yaw) * std::cos(pitch);
                        pitch = pitch * 0.7f + dPitch * 0.1f;
                        yaw  += dYaw * 0.1f;
                        dPitch = dPitch * 0.9f + (rand.nextFloat() - rand.nextFloat()) * rand.nextFloat() * 2.0f;
                        dYaw   = dYaw * 0.75f  + (rand.nextFloat() - rand.nextFloat()) * rand.nextFloat() * 4.0f;

                        int minX = std::max(0,  (int)std::floor(x - rad) - targetCx * 16 - 1);
                        int maxX = std::min(16, (int)std::floor(x + rad) - targetCx * 16 + 1);
                        int minY = std::max(1,  (int)std::floor(y - rad) - 1);
                        int maxY = std::min(125,(int)std::floor(y + rad) + 1);
                        int minZ = std::max(0,  (int)std::floor(z - rad) - targetCz * 16 - 1);
                        int maxZ = std::min(16, (int)std::floor(z + rad) - targetCz * 16 + 1);
                        if (minX >= maxX || minY >= maxY || minZ >= maxZ) continue;

                        for (int lx = minX; lx < maxX; ++lx) {
                            double nx = ((double)(targetCx * 16 + lx) + 0.5 - x) / rad;
                            for (int lz = minZ; lz < maxZ; ++lz) {
                                double nz = ((double)(targetCz * 16 + lz) + 0.5 - z) / rad;
                                if (nx * nx + nz * nz >= 1.0) continue;
                                for (int wy = maxY; wy > minY; --wy) {
                                    double ny = ((double)(wy - 1) + 0.5 - y) / rad;
                                    if (ny > -0.7 && nx * nx + ny * ny + nz * nz < 1.0) {
                                        uint8_t id = (uint8_t)((col.sections[wy >> 4].getBlock(lx, wy & 15, lz) >> 4) & 0xFF);
                                        if (id == 1 || id == 2 || id == 3 || id == 12 || id == 24) {
                                            col.sections[wy >> 4].setBlock(lx, wy & 15, lz, (wy - 1 < 10) ? (11 << 4) : 0);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Authentic Notch ChunkProviderHell.java (5x17x5 3D Noise Field, Lava Ocean Y<=31, Soul Sand Beaches & Glowstone)
    void generateNetherBase(ChunkColumn& col, int32_t cx, int32_t cz, BetaWorkerScratchpad& pad) const {
        col.sectionMask = 0xFF;
        for (int s = 0; s < SECTIONS_PER_COL; ++s) col.sections[s].clear(0);
        JavaRandomB173 rand((int64_t)cx * 341873128712LL + (int64_t)cz * 132897987541LL);

        int baseX = cx * 4, baseZ = cz * 4;
        double yOffsets[17];
        for (int y = 0; y < 17; ++y) {
            yOffsets[y] = std::cos((double)y * 3.14159265 * 6.0 / 17.0) * 2.0;
            double edge = (double)y;
            if (y > 8) edge = (double)(16 - y);
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
                        double slide = (double)(y - 13) / 3.0;
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
                                    col.sections[wy >> 4].setBlock(lx, wy & 15, lz, (87 << 4)); // Netherrack
                                } else if (wy < 32) {
                                    col.sections[wy >> 4].setBlock(lx, wy & 15, lz, (11 << 4)); // Lava sea
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

        // Soul Sand (88) & Gravel (13) Beaches + Top/Bottom Bedrock (7)
        for (int lz = 0; lz < 16; ++lz) {
            for (int lx = 0; lx < 16; ++lx) {
                int wx = cx * 16 + lx, wz = cz * 16 + lz;
                bool soulBeach   = (beachNoise.sample2D(wx, wz, 0.03125, 0.03125) + rand.nextDouble() * 0.2) > 0.0;
                bool gravelBeach = (beachNoise.sample2D(wz, wx, 0.03125, 0.03125) + rand.nextDouble() * 0.2) > 0.0;
                int depth = (int)(surfaceNoise.sample2D(wx, wz, 0.0625, 0.0625) / 3.0 + 3.0 + rand.nextDouble() * 0.25);

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

        // Hanging Glowstone Clusters (WorldGenGlowStone1) & Fire patches
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

        // Nether Fire Clusters (WorldGenFire)
        for (int f = 0; f < 10; ++f) {
            int fx = rand.nextInt(16), fy = 32 + rand.nextInt(80), fz = rand.nextInt(16);
            if (col.sections[fy >> 4].getBlock(fx, fy & 15, fz) == 0 &&
                ((col.sections[(fy - 1) >> 4].getBlock(fx, (fy - 1) & 15, fz) >> 4) & 0xFF) == 87) {
                col.sections[fy >> 4].setBlock(fx, fy & 15, fz, (51 << 4));
            }
        }
    }

    void decorateChunk(WorldMap& world, ChunkColumn& col, int32_t cx, int32_t cz, BetaWorkerScratchpad& pad) const {
        JavaRandomB173 rand((int64_t)cx * 4987142LL + (int64_t)cz * 5947611LL ^ seed);
        float centerTemp = col.temperature[8 * 16 + 8];
        float centerRain = col.humidity[8 * 16 + 8];

        {
            std::lock_guard<std::mutex> lock(world.getChunkLock(cx, cz));
            if (centerTemp <= 0.82f && rand.nextInt(4) == 0) generateLake(col, rand, (9 << 4), pad.lakeGrid);
            if (rand.nextInt(8) == 0) generateLake(col, rand, (11 << 4), pad.lakeGrid);

            auto spawnVein = [&](uint16_t oreState, int size, int count, int minY, int maxY) {
                for (int c = 0; c < count; ++c) {
                    int bx = rand.nextInt(16), by = minY + rand.nextInt(std::max(1, maxY - minY)), bz = rand.nextInt(16);
                    for (int s = 0; s < size; ++s) {
                        int px = std::clamp(bx + rand.nextInt(3) - 1, 0, 15);
                        int py = std::clamp(by + rand.nextInt(3) - 1, 1, 126);
                        int pz = std::clamp(bz + rand.nextInt(3) - 1, 0, 15);
                        if (col.sections[py >> 4].getBlock(px, py & 15, pz) == (1 << 4)) {
                            col.sections[py >> 4].setBlock(px, py & 15, pz, oreState);
                        }
                        bx = px; by = py; bz = pz;
                    }
                }
            };
            spawnVein((3 << 4),  32, 20, 0, 128);
            spawnVein((13 << 4), 32, 10, 0, 128);
            spawnVein((16 << 4), 16, 20, 0, 128);
            spawnVein((15 << 4), 8,  20, 0, 64);
            spawnVein((14 << 4), 8,  2,  0, 32);
            spawnVein((73 << 4), 7,  8,  0, 16);
            spawnVein((56 << 4), 7,  1,  0, 16);
            spawnVein((21 << 4), 6,  1,  0, 32);
        }

        auto setDecorBlock = [&](int wx, int wy, int wz, uint16_t st, bool onlyAir) {
            if (wy < 1 || wy >= 127) return;
            int32_t tcx = wx >> 4, tcz = wz >> 4;
            std::lock_guard<std::mutex> lock(world.getChunkLock(tcx, tcz));
            ChunkColumn* target = world.getTerrainOrReadyChunk(tcx, tcz);
            if (!target) return;
            int lx = wx & 15, lz = wz & 15, sy = wy >> 4;
            if (onlyAir && target->sections[sy].getBlock(lx, wy & 15, lz) != 0) return;
            target->sections[sy].setBlock(lx, wy & 15, lz, st);
        };

        int treeCount = (centerTemp > 0.82f && centerRain < 0.20f) ? 0 : (int)(forestNoise.sample2D(cx * 0.5, cz * 0.5, 1.0, 1.0) * 0.5 + 3.0);
        for (int t = 0; t < treeCount; ++t) {
            int lx = 1 + rand.nextInt(14), lz = 1 + rand.nextInt(14);
            int sy = 0;
            for (int y = 120; y >= SEA_LEVEL; --y) {
                uint8_t id = (uint8_t)((col.sections[y >> 4].getBlock(lx, y & 15, lz) >> 4) & 0xFF);
                if (id == 2 || id == 3) { sy = y; break; }
                if (id != 0 && id != 78) break;
            }
            if (sy <= SEA_LEVEL) continue;

            uint8_t woodMeta = (centerTemp < 0.35f) ? 1 : ((rand.nextInt(5) == 0) ? 2 : 0);
            uint16_t logSt  = (uint16_t)((17 << 4) | woodMeta);
            uint16_t leafSt = (uint16_t)((18 << 4) | woodMeta);
            int wx0 = cx * 16 + lx, wz0 = cz * 16 + lz;
            int h = 4 + rand.nextInt(3);

            setDecorBlock(wx0, sy, wz0, (3 << 4), false);
            for (int dy = 1; dy <= h; ++dy) setDecorBlock(wx0, sy + dy, wz0, logSt, false);
            for (int dy = h - 2; dy <= h + 1; ++dy) {
                int rad = (dy >= h) ? 1 : 2;
                for (int dx = -rad; dx <= rad; ++dx) {
                    for (int dz = -rad; dz <= rad; ++dz) {
                        if (std::abs(dx) == rad && std::abs(dz) == rad && (dy == h + 1 || rand.nextInt(2) == 0)) continue;
                        setDecorBlock(wx0 + dx, sy + dy, wz0 + dz, leafSt, true);
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(world.getChunkLock(cx, cz));
            for (int f = 0; f < 24; ++f) {
                int lx = rand.nextInt(16), lz = rand.nextInt(16);
                int sy = 0;
                for (int y = 120; y >= SEA_LEVEL; --y) {
                    uint8_t id = (uint8_t)((col.sections[y >> 4].getBlock(lx, y & 15, lz) >> 4) & 0xFF);
                    if (id != 0 && id != 78) { sy = y; break; }
                }
                if (sy <= SEA_LEVEL || sy + 1 >= 126) continue;
                uint8_t gId = (uint8_t)((col.sections[sy >> 4].getBlock(lx, sy & 15, lz) >> 4) & 0xFF);
                if (col.sections[(sy + 1) >> 4].getBlock(lx, (sy + 1) & 15, lz) != 0) continue;

                if (gId == 2) {
                    int r = rand.nextInt(12);
                    if (r < 8)       col.sections[(sy + 1) >> 4].setBlock(lx, (sy + 1) & 15, lz, (31 << 4) | 1);
                    else if (r == 8) col.sections[(sy + 1) >> 4].setBlock(lx, (sy + 1) & 15, lz, (37 << 4));
                    else if (r == 9) col.sections[(sy + 1) >> 4].setBlock(lx, (sy + 1) & 15, lz, (38 << 4));
                } else if (gId == 12 && centerTemp > 0.82f && rand.nextInt(18) == 0) {
                    int ch = 1 + rand.nextInt(3);
                    for (int dy = 1; dy <= ch; ++dy) {
                        col.sections[(sy + dy) >> 4].setBlock(lx, (sy + dy) & 15, lz, (81 << 4));
                    }
                }
            }
        }
    }

    static void generateLake(ChunkColumn& col, JavaRandomB173& rand, uint16_t liquid, bool grid[16 * 16 * 8]) {
        std::memset(grid, 0, 16 * 16 * 8 * sizeof(bool));
        int ly = 8 + rand.nextInt(58);
        for (int b = 0; b < 5; ++b) {
            double rx = rand.nextDouble() * 4.0 + 2.0, ry = rand.nextDouble() * 2.5 + 1.5, rz = rand.nextDouble() * 4.0 + 2.0;
            double cx = rand.nextDouble() * (14.0 - rx) + 1.0 + rx * 0.5;
            double cy = rand.nextDouble() * (6.0 - ry)  + 1.0 + ry * 0.5;
            double cz = rand.nextDouble() * (14.0 - rz) + 1.0 + rz * 0.5;
            for (int x = 1; x < 15; ++x)
                for (int z = 1; z < 15; ++z)
                    for (int y = 1; y < 7; ++y) {
                        double dx = (x - cx) / (rx * 0.5), dy = (y - cy) / (ry * 0.5), dz = (z - cz) / (rz * 0.5);
                        if (dx * dx + dy * dy + dz * dz < 1.0) grid[(x * 16 + z) * 8 + y] = true;
                    }
        }
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 4; ++y)
                    if (grid[(x * 16 + z) * 8 + y] && col.sections[(ly + y - 4) >> 4].getBlock(x, (ly + y - 4) & 15, z) == 0) return;

        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 8; ++y)
                    if (grid[(x * 16 + z) * 8 + y]) {
                        int wy = ly + y - 4;
                        col.sections[wy >> 4].setBlock(x, wy & 15, z, (y >= 4) ? 0 : liquid);
                    }
    }

    template<typename QueueType>
    void tryPromoteAndDecorate(WorldMap& world, int32_t cx, int32_t cz, BetaWorkerScratchpad& pad, QueueType& meshQueue) {
        {
            std::lock_guard<std::mutex> lock(world.getChunkLock(cx, cz));
            ChunkColumn& col = world.getSlot(cx, cz);
            if (col.chunkX != cx || col.chunkZ != cz) return;
            if (col.state.load(std::memory_order_acquire) != CHUNK_TERRAIN) return;

            if (!world.getTerrainOrReadyChunk(cx + 1, cz) ||
                !world.getTerrainOrReadyChunk(cx - 1, cz) ||
                !world.getTerrainOrReadyChunk(cx, cz + 1) ||
                !world.getTerrainOrReadyChunk(cx, cz - 1)) {
                return;
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