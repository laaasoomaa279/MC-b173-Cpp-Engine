#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "assets/recipes_b173.hpp"
#include "world/mcregion_nbt.hpp"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <mutex>

// ============================================================================
// CANONICAL SPATIAL VOXEL WORLD MATRIX & LIGHT ENGINE (SPEC-2011)
// ============================================================================
// Technical Subsystem Architecture:
// 1. Toroidal Fixed-Size Chunk Column Grid:
//    - 32x32 circular column array (GRID_DIM = 32, GRID_MASK = 31).
//    - Each column spans 16x128x16 voxels partitioned into eight 16x16x16 sections.
//    - Memory layout guarantees alignas(64) cache-line affinity per section.
// 2. Dual-Channel Asynchronous Luminance Propagation (Flood-Fill BFS):
//    - SkyLight & BlockLight nibbles packed into discrete 2048-byte arrays.
//    - Real-time BFS flood queues propagate attenuation gradients across 6-way
//      orthogonal neighbor nodes upon block mutation.
//    - Cross-boundary dirty marking forces neighbor section re-meshing, completely
//      eliminating dark unlit boundary artifacts at chunk seams.
// 3. Photometric Celestial Geometry & Ambient Floor:
//    - Solar inclination angle computed via piecewise cosine trajectory:
//        f1 = (dayTime % 24000) / 24000.0 - 0.25;
//        celestialAngle = f1 + (0.5 - cos(f1 * PI) * 0.5 - f1) / 3.0;
//    - Ambient light floor strictly guarantees a normalized 0.05 luminance
//      baseline, preventing submerged voxels from falling into pitch-black void.
// 4. Persistence & Tile Entity Serialization:
//    - Fully compliant McRegion binary column serializer for Chest, Furnace,
//      Signpost, and Spawner tile entities with Big-Endian NBT serialization.
// ============================================================================

constexpr int CHUNK_WIDTH      = 16;
constexpr int CHUNK_HEIGHT     = 128;
constexpr int SECTIONS_PER_COL = 8;
constexpr int SEA_LEVEL        = 64;
constexpr int GRID_DIM         = 32;
constexpr int GRID_MASK        = GRID_DIM - 1;

#include "world/world_fluids.hpp"

enum ChunkState : uint8_t {
    CHUNK_EMPTY      = 0,
    CHUNK_GENERATING = 1,
    CHUNK_TERRAIN    = 2,
    CHUNK_DECORATING = 3,
    CHUNK_READY      = 4
};

struct ChunkCoord {
    int32_t cx = 0;
    int32_t cz = 0;
};

struct BlockPosKey {
    int32_t x = 0, y = 0, z = 0;
    bool operator==(const BlockPosKey& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct BlockPosHasher {
    size_t operator()(const BlockPosKey& p) const noexcept {
        return ((size_t)(uint32_t)p.x * 73856093ULL) ^
               ((size_t)(uint32_t)p.y * 19349663ULL) ^
               ((size_t)(uint32_t)p.z * 83492791ULL);
    }
};

struct TileEntityData {
    std::string id;
    int32_t     x = 0, y = 0, z = 0;
    ItemStack   slots[27]{};
    int16_t     burnTime    = 0;
    int16_t     maxBurnTime = 0;
    int16_t     cookTime    = 0;
    std::string signLines[4];
};

struct FallingBlockEntity {
    uint16_t state = 0;
    float    x = 0.0f, y = 0.0f, z = 0.0f;
    float    vy = 0.0f;
    int      fallTicks = 0;
};

struct ScheduledBlockTick {
    int32_t x = 0, y = 0, z = 0;
    int     ticksRemaining = 0;
};

struct DetachedDropEvent {
    uint16_t itemId = 0;
    uint16_t meta   = 0;
    uint8_t  count  = 1;
    float    x = 0.0f, y = 0.0f, z = 0.0f;
};

struct alignas(64) ChunkSection {
    uint16_t blocks[4096]{};
    uint8_t  skyLight[2048]{};
    uint8_t  blockLight[2048]{};

    static inline int idx(int x, int y, int z) { return (y << 8) | (z << 4) | x; }

    void clear(uint16_t fillState = 0) {
        for (int i = 0; i < 4096; ++i) blocks[i] = fillState;
        std::memset(skyLight,   0xFF, sizeof(skyLight));
        std::memset(blockLight, 0x00, sizeof(blockLight));
    }

    inline uint16_t getBlock(int x, int y, int z) const { return blocks[idx(x, y, z)]; }
    inline void     setBlock(int x, int y, int z, uint16_t st) { blocks[idx(x, y, z)] = st; }

    inline uint8_t getSkyLight(int x, int y, int z) const {
        int i = idx(x, y, z);
        return (i & 1) ? (skyLight[i >> 1] >> 4) : (skyLight[i >> 1] & 0x0F);
    }
    inline void setSkyLight(int x, int y, int z, uint8_t val) {
        int i = idx(x, y, z);
        uint8_t& b = skyLight[i >> 1];
        b = (i & 1) ? ((b & 0x0F) | ((val & 0x0F) << 4)) : ((b & 0xF0) | (val & 0x0F));
    }

    inline uint8_t getBlockLight(int x, int y, int z) const {
        int i = idx(x, y, z);
        return (i & 1) ? (blockLight[i >> 1] >> 4) : (blockLight[i >> 1] & 0x0F);
    }
    inline void setBlockLight(int x, int y, int z, uint8_t val) {
        int i = idx(x, y, z);
        uint8_t& b = blockLight[i >> 1];
        b = (i & 1) ? ((b & 0x0F) | ((val & 0x0F) << 4)) : ((b & 0xF0) | (val & 0x0F));
    }
};

struct ChunkColumn {
    int32_t              chunkX      = 0;
    int32_t              chunkZ      = 0;
    std::atomic<uint8_t> state{CHUNK_EMPTY};
    uint8_t              sectionMask = 0xFF;
    bool                 dirty       = false;
    uint8_t              heightMap[256]{};
    float                temperature[256]{};
    float                humidity[256]{};
    ChunkSection         sections[SECTIONS_PER_COL];
};

class WorldMap {
public:
    BlockRegistry                   registry;
    int64_t                         worldSeed      = 123456789LL;
    int32_t                         dimension      = 0; // 0 = Overworld, -1 = Nether
    std::string                     saveDir        = "saves/New World";
    int32_t                         centerChunkX   = 0;
    int32_t                         centerChunkZ   = 0;
    int                             renderDistance = 8;
    uint64_t                        worldTick      = 0;
    uint64_t                        dayTime        = 1000;
    uint32_t                        tickRng        = 0x1337BEEFu;

    AudioEngine*                    audioEngine    = nullptr;
    const BetaAssetManager*         assetManager   = nullptr;

    ChunkColumn                     grid[GRID_DIM * GRID_DIM];
    std::mutex                      slotLocks[64];

    mutable std::mutex              tileEntityMutex;
    std::unordered_map<BlockPosKey, TileEntityData, BlockPosHasher> tileEntities;
    std::vector<FallingBlockEntity> fallingBlocks;
    std::vector<ScheduledBlockTick> scheduledTicks;
    std::vector<DetachedDropEvent>  pendingDetachedDrops;
    std::unordered_set<uint64_t>    pendingRemeshChunks;

    std::string getDimensionSaveDir() const {
        return (dimension == -1) ? (saveDir + "/DIM-1") : saveDir;
    }

    inline ChunkColumn& getSlot(int32_t cx, int32_t cz) {
        return grid[((cz & GRID_MASK) << 5) | (cx & GRID_MASK)];
    }
    inline const ChunkColumn& getSlot(int32_t cx, int32_t cz) const {
        return grid[((cz & GRID_MASK) << 5) | (cx & GRID_MASK)];
    }
    inline std::mutex& getChunkLock(int32_t cx, int32_t cz) {
        return slotLocks[((cz & 7) << 3) | (cx & 7)];
    }

    inline ChunkColumn* getReadyChunk(int32_t cx, int32_t cz) {
        ChunkColumn& c = getSlot(cx, cz);
        if (c.chunkX == cx && c.chunkZ == cz && c.state.load(std::memory_order_acquire) == CHUNK_READY) {
            return &c;
        }
        return nullptr;
    }
    inline const ChunkColumn* getReadyChunk(int32_t cx, int32_t cz) const {
        const ChunkColumn& c = getSlot(cx, cz);
        if (c.chunkX == cx && c.chunkZ == cz && c.state.load(std::memory_order_acquire) == CHUNK_READY) {
            return &c;
        }
        return nullptr;
    }

    inline ChunkColumn* getTerrainOrReadyChunk(int32_t cx, int32_t cz) {
        ChunkColumn& c = getSlot(cx, cz);
        uint8_t st = c.state.load(std::memory_order_acquire);
        if (c.chunkX == cx && c.chunkZ == cz && (st >= CHUNK_TERRAIN)) {
            return &c;
        }
        return nullptr;
    }
    inline const ChunkColumn* getTerrainOrReadyChunk(int32_t cx, int32_t cz) const {
        const ChunkColumn& c = getSlot(cx, cz);
        uint8_t st = c.state.load(std::memory_order_acquire);
        if (c.chunkX == cx && c.chunkZ == cz && (st >= CHUNK_TERRAIN)) {
            return &c;
        }
        return nullptr;
    }

    uint16_t getBlockAt(int wx, int wy, int wz) const {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return 0;
        const ChunkColumn* col = getReadyChunk(wx >> 4, wz >> 4);
        if (!col) return 0;
        return col->sections[wy >> 4].getBlock(wx & 15, wy & 15, wz & 15);
    }

    uint8_t getSkyLightAt(int wx, int wy, int wz) const {
        if (dimension == -1) return 2;
        if (wy >= CHUNK_HEIGHT) return 15;
        if (wy < 0) return 0;
        const ChunkColumn* col = getReadyChunk(wx >> 4, wz >> 4);
        if (!col) return 15;
        return col->sections[wy >> 4].getSkyLight(wx & 15, wy & 15, wz & 15);
    }

    uint8_t getBlockLightAt(int wx, int wy, int wz) const {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return 0;
        const ChunkColumn* col = getReadyChunk(wx >> 4, wz >> 4);
        if (!col) return 0;
        return col->sections[wy >> 4].getBlockLight(wx & 15, wy & 15, wz & 15);
    }

    int getTopSolidY(int wx, int wz) const {
        const ChunkColumn* col = getReadyChunk(wx >> 4, wz >> 4);
        if (!col) return 64;
        return static_cast<int>(col->heightMap[(wz & 15) * 16 + (wx & 15)]);
    }

    void markChunkAndEdgeNeighborsDirty(int wx, int wz) {
        int32_t cx = wx >> 4, cz = wz >> 4;
        auto pushKey = [&](int32_t x, int32_t z) {
            pendingRemeshChunks.insert((static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z));
        };
        pushKey(cx, cz);
        if ((wx & 15) == 0)  pushKey(cx - 1, cz);
        if ((wx & 15) == 15) pushKey(cx + 1, cz);
        if ((wz & 15) == 0)  pushKey(cx, cz - 1);
        if ((wz & 15) == 15) pushKey(cx, cz + 1);
    }

    inline int getFluidTickRate(uint8_t blockId) const {
        return WorldFluids::getFluidTickRate(blockId, dimension);
    }

    bool checkForHarden(int wx, int wy, int wz) {
        return WorldFluids::checkForHarden(*this, wx, wy, wz);
    }

    void tickFluidFlow(int wx, int wy, int wz, uint16_t state) {
        WorldFluids::tickFluidFlow(*this, wx, wy, wz, state);
    }

    void tickRandomBlock(int wx, int wy, int wz, uint16_t state) {
        WorldFluids::tickRandomBlock(*this, wx, wy, wz, state);
    }

    void tickFireBlock(int wx, int wy, int wz, uint16_t state) {
        WorldFluids::tickFireBlock(*this, wx, wy, wz, state);
    }

    bool setBlockAt(int wx, int wy, int wz, uint16_t newState, bool notifyNeighbors = true) {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return false;
        int32_t cx = wx >> 4, cz = wz >> 4;
        ChunkColumn* col = getReadyChunk(cx, cz);
        if (!col) return false;

        int lx = wx & 15, lz = wz & 15, sy = wy >> 4;
        uint16_t oldState = col->sections[sy].getBlock(lx, wy & 15, lz);
        if (oldState == newState) return true;

        uint8_t oldSkyLight = col->sections[sy].getSkyLight(lx, wy & 15, lz);
        uint8_t oldBlkLight = col->sections[sy].getBlockLight(lx, wy & 15, lz);

        col->sections[sy].setBlock(lx, wy & 15, lz, newState);
        col->dirty = true;
        if (newState != 0) col->sectionMask |= (1 << sy);
        markChunkAndEdgeNeighborsDirty(wx, wz);

        uint8_t oldId = static_cast<uint8_t>((oldState >> 4) & 0xFF);
        uint8_t newId = static_cast<uint8_t>((newState >> 4) & 0xFF);
        if (oldId != newId) {
            if (oldId == 54 || oldId == 61 || oldId == 62 || oldId == 63 || oldId == 68 || oldId == 52) {
                std::lock_guard<std::mutex> lock(tileEntityMutex);
                auto it = tileEntities.find({wx, wy, wz});
                if (it != tileEntities.end()) {
                    for (int s = 0; s < 27; ++s) {
                        if (!it->second.slots[s].isEmpty()) {
                            pendingDetachedDrops.push_back({ it->second.slots[s].id, it->second.slots[s].damage, it->second.slots[s].count, wx + 0.5f, wy + 0.5f, wz + 0.5f });
                        }
                    }
                    tileEntities.erase(it);
                }
            }
            if (newId == 54 || newId == 61 || newId == 63 || newId == 68 || newId == 52) {
                std::lock_guard<std::mutex> lock(tileEntityMutex);
                TileEntityData te;
                te.id = (newId == 54) ? "Chest" : ((newId == 61) ? "Furnace" : ((newId == 52) ? "MobSpawner" : "Sign"));
                te.x = wx; te.y = wy; te.z = wz;
                tileEntities[{wx, wy, wz}] = std::move(te);
            }
        }

        updateLightingAt(wx, wy, wz, oldSkyLight, oldBlkLight);

        if (notifyNeighbors) {
            if (newId == 8 || newId == 9 || newId == 10 || newId == 11) {
                if (checkForHarden(wx, wy, wz)) {
                    return true;
                }
            }

            static constexpr int dirs[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
            for (const auto& d : dirs) {
                int nx = wx + d[0], ny = wy + d[1], nz = wz + d[2];
                uint16_t nSt = getBlockAt(nx, ny, nz);
                uint8_t  nId = static_cast<uint8_t>((nSt >> 4) & 0xFF);
                if (nId == 0) continue;

                if (nId == 10 || nId == 11) {
                    if (checkForHarden(nx, ny, nz)) continue;
                }

                if (nId == 9 || nId == 11) {
                    uint8_t wakeId = (nId == 9) ? 8 : 10;
                    setBlockAt(nx, ny, nz, static_cast<uint16_t>((wakeId << 4) | (nSt & 0x0F)), false);
                    scheduleTick(nx, ny, nz, getFluidTickRate(wakeId));
                } else if (nId == 8 || nId == 10) {
                    scheduleTick(nx, ny, nz, getFluidTickRate(nId));
                } else if (nId == 12 || nId == 13) {
                    scheduleTick(nx, ny, nz, 3);
                } else if (nId == 18) {
                    uint8_t lMeta = static_cast<uint8_t>(nSt & 0x0F);
                    if ((lMeta & 0x04) == 0 && (lMeta & 0x08) == 0) {
                        setBlockAt(nx, ny, nz, static_cast<uint16_t>((18 << 4) | (lMeta | 0x08)), false);
                    }
                } else if (!canBlockStay(nx, ny, nz, nSt)) {
                    uint16_t dropId = WorldFluids::resolveSupportBreakDrop(nId);
                    setBlockAt(nx, ny, nz, 0, true);
                    if (dropId != 0) {
                        pendingDetachedDrops.push_back({ dropId, 0, 1, nx + 0.5f, ny + 0.3f, nz + 0.5f });
                    }
                }
            }

            if (newId == 8 || newId == 9 || newId == 10 || newId == 11) {
                scheduleTick(wx, wy, wz, getFluidTickRate(newId));
            } else if (newId == 12 || newId == 13) {
                scheduleTick(wx, wy, wz, 3);
            } else if (newId == 51) {
                scheduleTick(wx, wy, wz, 40);
            }
        }
        return true;
    }

    bool canBlockStay(int wx, int wy, int wz, uint16_t state) const {
        uint8_t id   = static_cast<uint8_t>((state >> 4) & 0xFF);
        uint8_t meta = static_cast<uint8_t>(state & 0x0F);
        uint16_t below = getBlockAt(wx, wy - 1, wz);
        uint8_t belowId = static_cast<uint8_t>((below >> 4) & 0xFF);

        // Snow layer (78): ONLY sustained by a complete solid opaque cube
        if (id == 78) {
            return registry.get(below).canSupportSnow();
        }

        if (id == 6 || id == 31 || id == 37 || id == 38) {
            return belowId == 2 || belowId == 3 || belowId == 60;
        }
        if (id == 59) return belowId == 60;
        if (id == 39 || id == 40) return registry.get(below).isOpaque && getSkyLightAt(wx, wy, wz) < 13;
        if (id == 83) {
            if (belowId == 83) return true;
            if (belowId != 2 && belowId != 3 && belowId != 12) return false;
            int adj[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
            for (const auto& a : adj) {
                uint8_t wId = static_cast<uint8_t>((getBlockAt(wx + a[0], wy - 1, wz + a[1]) >> 4) & 0xFF);
                if (wId == 8 || wId == 9) return true;
            }
            return false;
        }
        if (id == 81) {
            if (belowId != 12 && belowId != 81) return false;
            int adj[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
            for (const auto& a : adj) {
                if (registry.get(getBlockAt(wx + a[0], wy, wz + a[1])).isSolid) return false;
            }
            return true;
        }
        if (id == 50 || id == 75 || id == 76) {
            if (meta == 1) return registry.get(getBlockAt(wx - 1, wy, wz)).isSolid;
            if (meta == 2) return registry.get(getBlockAt(wx + 1, wy, wz)).isSolid;
            if (meta == 3) return registry.get(getBlockAt(wx, wy, wz - 1)).isSolid;
            if (meta == 4) return registry.get(getBlockAt(wx, wy, wz + 1)).isSolid;
            return registry.get(below).isSolid || belowId == 85;
        }
        if (id == 69) {
            uint8_t m = meta & 0x07;
            if (m == 1) return registry.get(getBlockAt(wx - 1, wy, wz)).isSolid;
            if (m == 2) return registry.get(getBlockAt(wx + 1, wy, wz)).isSolid;
            if (m == 3) return registry.get(getBlockAt(wx, wy, wz - 1)).isSolid;
            if (m == 4) return registry.get(getBlockAt(wx, wy, wz + 1)).isSolid;
            return registry.get(below).isSolid;
        }
        if (id == 77) {
            uint8_t m = meta & 0x07;
            if (m == 1) return registry.get(getBlockAt(wx - 1, wy, wz)).isSolid;
            if (m == 2) return registry.get(getBlockAt(wx + 1, wy, wz)).isSolid;
            if (m == 3) return registry.get(getBlockAt(wx, wy, wz - 1)).isSolid;
            return registry.get(getBlockAt(wx, wy, wz + 1)).isSolid;
        }
        if (id == 65 || id == 68) {
            uint8_t m = meta & 0x07;
            if (m == 2) return registry.get(getBlockAt(wx, wy, wz + 1)).isSolid;
            if (m == 3) return registry.get(getBlockAt(wx, wy, wz - 1)).isSolid;
            if (m == 4) return registry.get(getBlockAt(wx + 1, wy, wz)).isSolid;
            return registry.get(getBlockAt(wx - 1, wy, wz)).isSolid;
        }
        if (id == 55 || id == 93 || id == 94 || id == 70 || id == 72 || id == 27 || id == 28 || id == 66 || id == 92) {
            return registry.get(below).isSolid;
        }
        if (id == 64 || id == 71) {
            if (meta & 0x08) return ((getBlockAt(wx, wy - 1, wz) >> 4) & 0xFF) == id;
            return registry.get(below).isSolid && ((getBlockAt(wx, wy + 1, wz) >> 4) & 0xFF) == id;
        }
        if (id == 51) {
            if (registry.get(below).isSolid) return true;
            static constexpr int dirs[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
            for (const auto& d : dirs) {
                if (registry.get(getBlockAt(wx + d[0], wy + d[1], wz + d[2])).isFlammable) return true;
            }
            return false;
        }
        return true;
    }

    void scheduleTick(int wx, int wy, int wz, int delay) {
        for (auto& tk : scheduledTicks) {
            if (tk.x == wx && tk.y == wy && tk.z == wz) {
                if (delay < tk.ticksRemaining) tk.ticksRemaining = delay;
                return;
            }
        }
        if (scheduledTicks.size() < 8192) {
            scheduledTicks.push_back({ wx, wy, wz, delay });
        }
    }

    void computeColumnInitialLighting(ChunkColumn& col, int32_t cx, int32_t cz) {
        struct LightNode { int16_t x, y, z; uint8_t level; };
        std::vector<LightNode> skyQ;
        std::vector<LightNode> blkQ;
        skyQ.reserve(2048);
        blkQ.reserve(1024);

        for (int lz = 0; lz < 16; ++lz) {
            for (int lx = 0; lx < 16; ++lx) {
                uint8_t sky = (dimension == 0) ? 15 : 2;
                int topY = 0;
                for (int wy = CHUNK_HEIGHT - 1; wy >= 0; --wy) {
                    int sy = wy >> 4;
                    uint16_t st = col.sections[sy].getBlock(lx, wy & 15, lz);
                    const BlockDef& def = registry.get(st);

                    if (dimension == 0) {
                        if (topY == 0 && (def.isSolid || def.lightOpacity > 0)) {
                            topY = wy + 1;
                        }
                        if (def.lightOpacity > 0) {
                            sky = (sky > def.lightOpacity) ? static_cast<uint8_t>(sky - def.lightOpacity) : 0;
                        }
                    } else {
                        if (wy < 120 && topY == 0 && def.isSolid) {
                            topY = wy + 1;
                        }
                    }

                    col.sections[sy].setSkyLight(lx, wy & 15, lz, sky);
                    col.sections[sy].setBlockLight(lx, wy & 15, lz, def.lightEmission);

                    if (dimension == 0 && sky > 1) {
                        skyQ.push_back({ static_cast<int16_t>(cx * 16 + lx), static_cast<int16_t>(wy), static_cast<int16_t>(cz * 16 + lz), sky });
                    }
                    if (def.lightEmission > 1) {
                        blkQ.push_back({ static_cast<int16_t>(cx * 16 + lx), static_cast<int16_t>(wy), static_cast<int16_t>(cz * 16 + lz), def.lightEmission });
                    }
                }
                col.heightMap[lz * 16 + lx] = static_cast<uint8_t>(topY > 0 ? topY : 64);
            }
        }

        static constexpr int dirs[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };

        size_t head = 0;
        while (head < skyQ.size()) {
            LightNode cur = skyQ[head++];
            if (cur.level <= 1) continue;
            for (const auto& d : dirs) {
                int nx = cur.x + d[0], ny = cur.y + d[1], nz = cur.z + d[2];
                if (ny < 0 || ny >= CHUNK_HEIGHT) continue;
                ChunkColumn* nbCol = getTerrainOrReadyChunk(nx >> 4, nz >> 4);
                if (!nbCol) continue;

                int nlx = nx & 15, nlz = nz & 15, nsy = ny >> 4;
                const BlockDef& nd = registry.get(nbCol->sections[nsy].getBlock(nlx, ny & 15, nlz));
                int drop = std::max(1, static_cast<int>(nd.lightOpacity));
                int nextL = static_cast<int>(cur.level) - drop;
                if (nextL > static_cast<int>(nbCol->sections[nsy].getSkyLight(nlx, ny & 15, nlz))) {
                    nbCol->sections[nsy].setSkyLight(nlx, ny & 15, nlz, static_cast<uint8_t>(nextL));
                    nbCol->dirty = true;
                    markChunkAndEdgeNeighborsDirty(nx, nz); // Fixes black seams across chunk boundaries
                    skyQ.push_back({ static_cast<int16_t>(nx), static_cast<int16_t>(ny), static_cast<int16_t>(nz), static_cast<uint8_t>(nextL) });
                }
            }
        }

        head = 0;
        while (head < blkQ.size()) {
            LightNode cur = blkQ[head++];
            if (cur.level <= 1) continue;
            for (const auto& d : dirs) {
                int nx = cur.x + d[0], ny = cur.y + d[1], nz = cur.z + d[2];
                if (ny < 0 || ny >= CHUNK_HEIGHT) continue;
                ChunkColumn* nbCol = getTerrainOrReadyChunk(nx >> 4, nz >> 4);
                if (!nbCol) continue;

                int nlx = nx & 15, nlz = nz & 15, nsy = ny >> 4;
                const BlockDef& nd = registry.get(nbCol->sections[nsy].getBlock(nlx, ny & 15, nlz));
                int drop = std::max(1, static_cast<int>(nd.lightOpacity));
                int nextL = static_cast<int>(cur.level) - drop;
                if (nextL > static_cast<int>(nbCol->sections[nsy].getBlockLight(nlx, ny & 15, nlz))) {
                    nbCol->sections[nsy].setBlockLight(nlx, ny & 15, nlz, static_cast<uint8_t>(nextL));
                    nbCol->dirty = true;
                    markChunkAndEdgeNeighborsDirty(nx, nz); // Fixes black seams across chunk boundaries
                    blkQ.push_back({ static_cast<int16_t>(nx), static_cast<int16_t>(ny), static_cast<int16_t>(nz), static_cast<uint8_t>(nextL) });
                }
            }
        }
    }

    template<typename QueueType>
    void flushDirtyLightMeshes(QueueType& meshQueue) {
        if (pendingRemeshChunks.empty()) return;
        for (uint64_t key : pendingRemeshChunks) {
            int32_t cx = static_cast<int32_t>(key >> 32);
            int32_t cz = static_cast<int32_t>(key & 0xFFFFFFFFu);
            if (getReadyChunk(cx, cz)) {
                meshQueue.push({ cx, cz });
            }
        }
        pendingRemeshChunks.clear();
    }

    template<typename QueueType>
    void tickSimulation(QueueType& meshQueue, AudioEngine* audio = nullptr, const BetaAssetManager* assets = nullptr) {
        if (audio)  audioEngine  = audio;
        if (assets) assetManager = assets;

        worldTick++;
        dayTime = (dayTime + 1) % 24000ULL;

        flushDirtyLightMeshes(meshQueue);

        // 1. TileEntity Smelting Simulation
        {
            std::lock_guard<std::mutex> lock(tileEntityMutex);
            for (auto& kv : tileEntities) {
                TileEntityData& te = kv.second;
                if (te.id != "Furnace") continue;
                bool wasBurning = te.burnTime > 0;
                if (te.burnTime > 0) te.burnTime--;

                ItemStack smeltOut = BetaRecipeRegistry::getSmeltingResult(te.slots[0]);
                bool canSmelt = !smeltOut.isEmpty() &&
                                (te.slots[2].isEmpty() || (te.slots[2].id == smeltOut.id && te.slots[2].damage == smeltOut.damage && te.slots[2].count < 64));

                if (te.burnTime == 0 && canSmelt) {
                    int fuelTicks = BetaRecipeRegistry::getFuelBurnTicks(te.slots[1]);
                    if (fuelTicks > 0) {
                        te.burnTime = te.maxBurnTime = static_cast<int16_t>(fuelTicks);
                        if (te.slots[1].id == ITEM_LAVA_BUCKET) te.slots[1].id = ITEM_BUCKET;
                        else if (--te.slots[1].count == 0) te.slots[1].clear();
                    }
                }

                if (te.burnTime > 0 && canSmelt) {
                    te.cookTime++;
                    if (te.cookTime >= 200) {
                        te.cookTime = 0;
                        if (te.slots[2].isEmpty()) te.slots[2] = smeltOut;
                        else te.slots[2].count++;
                        if (--te.slots[0].count == 0) te.slots[0].clear();
                    }
                } else {
                    te.cookTime = 0;
                }

                bool isBurning = te.burnTime > 0;
                if (wasBurning != isBurning) {
                    uint16_t curSt = getBlockAt(te.x, te.y, te.z);
                    uint8_t curId  = static_cast<uint8_t>((curSt >> 4) & 0xFF);
                    if (curId == 61 || curId == 62) {
                        uint16_t nextSt = static_cast<uint16_t>(((isBurning ? 62 : 61) << 4) | (curSt & 0x0F));
                        setBlockAt(te.x, te.y, te.z, nextSt, false);
                    }
                }
            }
        }

        // 2. Scheduled Block Ticks (Fluid Flow, Sand/Gravel Physics, Fire Tick)
        size_t count = scheduledTicks.size();
        for (size_t i = 0; i < count; ) {
            if (--scheduledTicks[i].ticksRemaining > 0) {
                ++i;
                continue;
            }
            ScheduledBlockTick tk = scheduledTicks[i];
            scheduledTicks[i] = scheduledTicks.back();
            scheduledTicks.pop_back();
            --count;

            uint16_t st = getBlockAt(tk.x, tk.y, tk.z);
            uint8_t id  = static_cast<uint8_t>((st >> 4) & 0xFF);

            if ((id == 12 || id == 13) && tk.y > 0) {
                uint16_t below = getBlockAt(tk.x, tk.y - 1, tk.z);
                if (registry.get(below).isReplaceable) {
                    setBlockAt(tk.x, tk.y, tk.z, 0, true);
                    fallingBlocks.push_back({ st, tk.x + 0.5f, static_cast<float>(tk.y), tk.z + 0.5f, 0.0f, 0 });
                }
            } else if (id == 8 || id == 9 || id == 10 || id == 11) {
                tickFluidFlow(tk.x, tk.y, tk.z, st);
            } else if (id == 51) {
                tickFireBlock(tk.x, tk.y, tk.z, st);
            }
        }

        // 3. Falling Sand & Gravel Kinematic Integration
        for (size_t i = 0; i < fallingBlocks.size(); ) {
            FallingBlockEntity& fb = fallingBlocks[i];
            fb.vy -= 0.04f;
            fb.y  += fb.vy;
            fb.vy *= 0.98f;
            fb.fallTicks++;

            int bx = static_cast<int>(std::floor(fb.x));
            int by = static_cast<int>(std::floor(fb.y));
            int bz = static_cast<int>(std::floor(fb.z));
            if (by <= 0 || !registry.get(getBlockAt(bx, by, bz)).isReplaceable || fb.fallTicks > 400) {
                int landY = std::clamp(by + 1, 1, CHUNK_HEIGHT - 1);
                if (registry.get(getBlockAt(bx, landY, bz)).isReplaceable) {
                    setBlockAt(bx, landY, bz, fb.state, true);
                } else {
                    pendingDetachedDrops.push_back({ static_cast<uint16_t>((fb.state >> 4) & 0xFF), 0, 1, fb.x, fb.y + 0.5f, fb.z });
                }
                fallingBlocks[i] = fallingBlocks.back();
                fallingBlocks.pop_back();
            } else {
                ++i;
            }
        }

        // 4. Canonical 3 Random Block Ticks per Active Section
        for (int dz = -renderDistance; dz <= renderDistance; ++dz) {
            for (int dx = -renderDistance; dx <= renderDistance; ++dx) {
                int32_t cx = centerChunkX + dx, cz = centerChunkZ + dz;
                ChunkColumn* col = getReadyChunk(cx, cz);
                if (!col) continue;

                for (int r = 0; r < 3; ++r) {
                    tickRng = tickRng * 1664525u + 1013904223u;
                    int lx = static_cast<int>(tickRng & 15u);
                    int lz = static_cast<int>((tickRng >> 8) & 15u);
                    int wy = static_cast<int>((tickRng >> 16) & 127u);
                    int sy = wy >> 4;
                    if (!(col->sectionMask & (1 << sy))) continue;

                    uint16_t st = col->sections[sy].getBlock(lx, wy & 15, lz);
                    if (st == 0) continue;

                    int wx = cx * 16 + lx;
                    int wz = cz * 16 + lz;
                    tickRandomBlock(wx, wy, wz, st);
                }
            }
        }

        flushDirtyLightMeshes(meshQueue);
    }

    float getSkyBrightness() const {
        if (dimension == -1) return 1.0f;
        int i = static_cast<int>(dayTime % 24000ULL);
        float f1 = static_cast<float>(i) / 24000.0f - 0.25f;
        if (f1 < 0.0f) f1 += 1.0f;
        if (f1 > 1.0f) f1 -= 1.0f;
        float f2 = f1;
        f1 = 0.5f - std::cos(f1 * 3.1415927f) / 2.0f;
        float celestialAngle = f2 + (f1 - f2) / 3.0f;

        float skyDarken = 1.0f - (std::cos(celestialAngle * 3.1415927f * 2.0f) * 2.0f + 0.5f);
        skyDarken = std::clamp(skyDarken, 0.0f, 1.0f);
        return std::max(0.05f, 1.0f - skyDarken * 0.8f); // 0.05 ambient floor prevents pitch-black void
    }

    Vec3 getSkyColor() const {
        if (dimension == -1) return { 0.20f, 0.03f, 0.03f };
        int i = static_cast<int>(dayTime % 24000ULL);
        float f1 = static_cast<float>(i) / 24000.0f - 0.25f;
        if (f1 < 0.0f) f1 += 1.0f;
        if (f1 > 1.0f) f1 -= 1.0f;
        float f2 = f1;
        f1 = 0.5f - std::cos(f1 * 3.1415927f) / 2.0f;
        float celestialAngle = f2 + (f1 - f2) / 3.0f;

        float b = std::clamp(std::cos(celestialAngle * 3.1415927f * 2.0f) * 2.0f + 0.5f, 0.0f, 1.0f);
        return { 0.53f * b, 0.72f * b, 1.00f * b };
    }

    void exportChunkToMcRegion(const ChunkColumn& col, McRegionChunkData& out) const {
        out.xPos             = col.chunkX;
        out.zPos             = col.chunkZ;
        out.lastUpdate       = static_cast<int64_t>(worldTick);
        out.terrainPopulated = true;
        std::memcpy(out.heightMap, col.heightMap, 256);

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    int xzy = (x * 16 + z) * 128 + y;
                    const ChunkSection& sec = col.sections[y >> 4];
                    uint16_t st = sec.getBlock(x, y & 15, z);
                    out.blocks[xzy] = static_cast<uint8_t>((st >> 4) & 0xFF);

                    int nibIdx = xzy >> 1;
                    uint8_t meta = static_cast<uint8_t>(st & 0x0F);
                    uint8_t sky  = sec.getSkyLight(x, y & 15, z);
                    uint8_t blk  = sec.getBlockLight(x, y & 15, z);
                    if (xzy & 1) {
                        out.data[nibIdx]       = (out.data[nibIdx] & 0x0F) | (meta << 4);
                        out.skyLight[nibIdx]   = (out.skyLight[nibIdx] & 0x0F) | (sky << 4);
                        out.blockLight[nibIdx] = (out.blockLight[nibIdx] & 0x0F) | (blk << 4);
                    } else {
                        out.data[nibIdx]       = (out.data[nibIdx] & 0xF0) | meta;
                        out.skyLight[nibIdx]   = (out.skyLight[nibIdx] & 0xF0) | sky;
                        out.blockLight[nibIdx] = (out.blockLight[nibIdx] & 0xF0) | blk;
                    }
                }
            }
        }

        out.tileEntities.clear();
        std::lock_guard<std::mutex> lock(tileEntityMutex);
        for (const auto& kv : tileEntities) {
            const TileEntityData& te = kv.second;
            if ((te.x >> 4) == col.chunkX && (te.z >> 4) == col.chunkZ) {
                SavedTileEntityNBT ste;
                ste.id       = te.id;
                ste.x        = te.x;
                ste.y        = te.y;
                ste.z        = te.z;
                ste.burnTime = te.burnTime;
                ste.cookTime = te.cookTime;
                for (int l = 0; l < 4; ++l) ste.signLines[l] = te.signLines[l];
                for (int s = 0; s < 27; ++s) {
                    if (!te.slots[s].isEmpty()) {
                        ste.items.push_back({
                            static_cast<int8_t>(s),
                            static_cast<int16_t>(te.slots[s].id),
                            static_cast<int8_t>(te.slots[s].count),
                            static_cast<int16_t>(te.slots[s].damage)
                        });
                    }
                }
                out.tileEntities.push_back(std::move(ste));
            }
        }
    }

    void importChunkFromMcRegion(const McRegionChunkData& in, ChunkColumn& col) {
        col.chunkX      = in.xPos;
        col.chunkZ      = in.zPos;
        col.sectionMask = 0xFF;
        col.dirty       = false;
        std::memcpy(col.heightMap, in.heightMap, 256);
        for (int i = 0; i < 256; ++i) { col.temperature[i] = 0.7f; col.humidity[i] = 0.6f; }

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    int xzy = (x * 16 + z) * 128 + y;
                    int nibIdx = xzy >> 1;
                    uint8_t id   = in.blocks[xzy];
                    uint8_t meta = (xzy & 1) ? (in.data[nibIdx] >> 4)       : (in.data[nibIdx] & 0x0F);
                    uint8_t sky  = (xzy & 1) ? (in.skyLight[nibIdx] >> 4)   : (in.skyLight[nibIdx] & 0x0F);
                    uint8_t blk  = (xzy & 1) ? (in.blockLight[nibIdx] >> 4) : (in.blockLight[nibIdx] & 0x0F);

                    ChunkSection& sec = col.sections[y >> 4];
                    sec.setBlock(x, y & 15, z, static_cast<uint16_t>((id << 4) | meta));
                    sec.setSkyLight(x, y & 15, z, sky);
                    sec.setBlockLight(x, y & 15, z, blk);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(tileEntityMutex);
            for (auto it = tileEntities.begin(); it != tileEntities.end(); ) {
                if ((it->first.x >> 4) == in.xPos && (it->first.z >> 4) == in.zPos) {
                    it = tileEntities.erase(it);
                } else {
                    ++it;
                }
            }
            for (const auto& ste : in.tileEntities) {
                TileEntityData te;
                te.id          = ste.id;
                te.x           = ste.x;
                te.y           = ste.y;
                te.z           = ste.z;
                te.burnTime    = ste.burnTime;
                te.maxBurnTime = (ste.burnTime > 0) ? ste.burnTime : 200;
                te.cookTime    = ste.cookTime;
                for (int l = 0; l < 4; ++l) te.signLines[l] = ste.signLines[l];
                for (const auto& sl : ste.items) {
                    if (sl.slot >= 0 && sl.slot < 27 && sl.id > 0 && sl.count > 0) {
                        te.slots[static_cast<int>(sl.slot)] = {
                            static_cast<uint16_t>(sl.id),
                            static_cast<uint16_t>(sl.damage),
                            static_cast<uint8_t>(sl.count)
                        };
                    }
                }
                tileEntities[{ te.x, te.y, te.z }] = std::move(te);
            }
        }

        col.state.store(CHUNK_READY, std::memory_order_release);
    }

    void saveAllDirtyChunks() {
        {
            std::lock_guard<std::mutex> lock(tileEntityMutex);
            for (const auto& kv : tileEntities) {
                ChunkColumn* col = getReadyChunk(kv.first.x >> 4, kv.first.z >> 4);
                if (col) col->dirty = true;
            }
        }

        std::string dimDir = getDimensionSaveDir();
        for (int i = 0; i < GRID_DIM * GRID_DIM; ++i) {
            ChunkColumn& col = grid[i];
            if (col.state.load(std::memory_order_acquire) == CHUNK_READY && col.dirty) {
                McRegionChunkData mcr;
                exportChunkToMcRegion(col, mcr);
                McRegionStorage::saveChunk(dimDir, mcr);
                col.dirty = false;
            }
        }
    }

    void updateLightingAt(int wx, int wy, int wz, uint8_t oldSky, uint8_t oldBlk) {
        struct LightNode { int x, y, z; uint8_t val; };
        static constexpr int dirs[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };

        int32_t cx = wx >> 4, cz = wz >> 4;
        ChunkColumn* col = getReadyChunk(cx, cz);
        if (!col) return;
        int lx = wx & 15, lz = wz & 15;

        const BlockDef& newDef = registry.get(col->sections[wy >> 4].getBlock(lx, wy & 15, lz));
        uint8_t newEmit = newDef.lightEmission;

        std::vector<LightNode> blkRemoveQ;
        std::vector<LightNode> blkFillQ;
        blkRemoveQ.reserve(256);
        blkFillQ.reserve(512);

        if (oldBlk > newEmit) {
            col->sections[wy >> 4].setBlockLight(lx, wy & 15, lz, 0);
            blkRemoveQ.push_back({ wx, wy, wz, oldBlk });
        }
        if (newEmit > 0) {
            col->sections[wy >> 4].setBlockLight(lx, wy & 15, lz, newEmit);
            blkFillQ.push_back({ wx, wy, wz, newEmit });
        }

        if (newDef.lightOpacity < 15) {
            for (const auto& d : dirs) {
                int nx = wx + d[0], ny = wy + d[1], nz = wz + d[2];
                if (ny < 0 || ny >= CHUNK_HEIGHT) continue;
                uint8_t nBlk = getBlockLightAt(nx, ny, nz);
                if (nBlk > 1) blkFillQ.push_back({ nx, ny, nz, nBlk });
            }
        }

        size_t head = 0;
        while (head < blkRemoveQ.size()) {
            LightNode cur = blkRemoveQ[head++];
            for (const auto& d : dirs) {
                int nx = cur.x + d[0], ny = cur.y + d[1], nz = cur.z + d[2];
                if (ny < 0 || ny >= CHUNK_HEIGHT) continue;
                ChunkColumn* nCol = getReadyChunk(nx >> 4, nz >> 4);
                if (!nCol) continue;

                int nlx = nx & 15, nlz = nz & 15, nsy = ny >> 4;
                uint8_t neighborLevel = nCol->sections[nsy].getBlockLight(nlx, ny & 15, nlz);
                if (neighborLevel != 0 && neighborLevel < cur.val) {
                    nCol->sections[nsy].setBlockLight(nlx, ny & 15, nlz, 0);
                    nCol->dirty = true;
                    markChunkAndEdgeNeighborsDirty(nx, nz);
                    blkRemoveQ.push_back({ nx, ny, nz, neighborLevel });
                } else if (neighborLevel >= cur.val) {
                    blkFillQ.push_back({ nx, ny, nz, neighborLevel });
                }
            }
        }

        head = 0;
        while (head < blkFillQ.size()) {
            LightNode cur = blkFillQ[head++];
            if (cur.val <= 1) continue;
            for (const auto& d : dirs) {
                int nx = cur.x + d[0], ny = cur.y + d[1], nz = cur.z + d[2];
                if (ny < 0 || ny >= CHUNK_HEIGHT) continue;
                ChunkColumn* nCol = getReadyChunk(nx >> 4, nz >> 4);
                if (!nCol) continue;

                int nlx = nx & 15, nlz = nz & 15, nsy = ny >> 4;
                const BlockDef& nd = registry.get(nCol->sections[nsy].getBlock(nlx, ny & 15, nlz));
                int drop = std::max(1, static_cast<int>(nd.lightOpacity));
                int nextL = static_cast<int>(cur.val) - drop;
                if (nextL > static_cast<int>(nCol->sections[nsy].getBlockLight(nlx, ny & 15, nlz))) {
                    nCol->sections[nsy].setBlockLight(nlx, ny & 15, nlz, static_cast<uint8_t>(nextL));
                    nCol->dirty = true;
                    markChunkAndEdgeNeighborsDirty(nx, nz);
                    blkFillQ.push_back({ nx, ny, nz, static_cast<uint8_t>(nextL) });
                }
            }
        }

        if (dimension == -1) return;

        std::vector<LightNode> skyRemoveQ;
        std::vector<LightNode> skyFillQ;
        skyRemoveQ.reserve(256);
        skyFillQ.reserve(512);

        if (oldSky > 0 && newDef.lightOpacity > 0) {
            col->sections[wy >> 4].setSkyLight(lx, wy & 15, lz, 0);
            skyRemoveQ.push_back({ wx, wy, wz, oldSky });
        }

        int topY = 0;
        uint8_t sky = 15;
        for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
            int sy = y >> 4;
            const BlockDef& def = registry.get(col->sections[sy].getBlock(lx, y & 15, lz));
            if (topY == 0 && (def.isSolid || def.lightOpacity > 0)) topY = y + 1;
            if (def.lightOpacity > 0) {
                sky = (sky > def.lightOpacity) ? static_cast<uint8_t>(sky - def.lightOpacity) : 0;
            }
            uint8_t prevS = col->sections[sy].getSkyLight(lx, y & 15, lz);
            if (sky > prevS) {
                col->sections[sy].setSkyLight(lx, y & 15, lz, sky);
                skyFillQ.push_back({ wx, y, wz, sky });
            } else if (sky < prevS && prevS > 0) {
                col->sections[sy].setSkyLight(lx, y & 15, lz, 0);
                skyRemoveQ.push_back({ wx, y, wz, prevS });
            }
        }
        col->heightMap[lz * 16 + lx] = static_cast<uint8_t>(topY);

        if (newDef.lightOpacity < 15) {
            for (const auto& d : dirs) {
                int nx = wx + d[0], ny = wy + d[1], nz = wz + d[2];
                if (ny < 0 || ny >= CHUNK_HEIGHT) continue;
                uint8_t nSky = getSkyLightAt(nx, ny, nz);
                if (nSky > 1) skyFillQ.push_back({ nx, ny, nz, nSky });
            }
        }

        head = 0;
        while (head < skyRemoveQ.size()) {
            LightNode cur = skyRemoveQ[head++];
            for (const auto& d : dirs) {
                int nx = cur.x + d[0], ny = cur.y + d[1], nz = cur.z + d[2];
                if (ny < 0 || ny >= CHUNK_HEIGHT) continue;
                ChunkColumn* nCol = getReadyChunk(nx >> 4, nz >> 4);
                if (!nCol) continue;

                int nlx = nx & 15, nlz = nz & 15, nsy = ny >> 4;
                uint8_t neighborLevel = nCol->sections[nsy].getSkyLight(nlx, ny & 15, nlz);
                bool isDirectDown15 = (d[1] == -1 && cur.val == 15 && neighborLevel == 15);
                if (neighborLevel != 0 && (neighborLevel < cur.val || isDirectDown15)) {
                    nCol->sections[nsy].setSkyLight(nlx, ny & 15, nlz, 0);
                    nCol->dirty = true;
                    markChunkAndEdgeNeighborsDirty(nx, nz);
                    skyRemoveQ.push_back({ nx, ny, nz, neighborLevel });
                } else if (neighborLevel >= cur.val) {
                    skyFillQ.push_back({ nx, ny, nz, neighborLevel });
                }
            }
        }

        head = 0;
        while (head < skyFillQ.size()) {
            LightNode cur = skyFillQ[head++];
            if (cur.val <= 1) continue;
            for (const auto& d : dirs) {
                int nx = cur.x + d[0], ny = cur.y + d[1], nz = cur.z + d[2];
                if (ny < 0 || ny >= CHUNK_HEIGHT) continue;
                ChunkColumn* nCol = getReadyChunk(nx >> 4, nz >> 4);
                if (!nCol) continue;

                int nlx = nx & 15, nlz = nz & 15, nsy = ny >> 4;
                const BlockDef& nd = registry.get(nCol->sections[nsy].getBlock(nlx, ny & 15, nlz));
                int drop = (d[1] == -1 && cur.val == 15 && nd.lightOpacity == 0) ? 0 : std::max(1, static_cast<int>(nd.lightOpacity));
                int nextL = static_cast<int>(cur.val) - drop;
                if (nextL > static_cast<int>(nCol->sections[nsy].getSkyLight(nlx, ny & 15, nlz))) {
                    nCol->sections[nsy].setSkyLight(nlx, ny & 15, nlz, static_cast<uint8_t>(nextL));
                    nCol->dirty = true;
                    markChunkAndEdgeNeighborsDirty(nx, nz);
                    skyFillQ.push_back({ nx, ny, nz, static_cast<uint8_t>(nextL) });
                }
            }
        }
    }
};