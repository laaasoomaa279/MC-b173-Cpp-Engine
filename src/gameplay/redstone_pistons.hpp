#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "world/world.hpp"

#include <cstdint>
#include <cmath>
#include <array>
#include <vector>
#include <deque>
#include <unordered_set>
#include <algorithm>

// ============================================================================
// Redstone Circuits, Pistons & Explosions
// ============================================================================
//
//       * Strict Strong Power (isBlockGettingPowered) vs. Weak Power
//         (isBlockIndirectlyGettingPowered).
//       * Strongly powered solid opaque blocks (Repeater output, Torch On
//         directly below, or active Lever/Button/Pressure Plate/Detector Rail)
//         power adjacent Redstone Wires AND mechanisms.
//       * Weakly powered solid opaque blocks (Redstone Wire on top or pointing
//         into the block, including single dot-wire rule) turn OFF attached
//         Redstone Torches, power Repeaters, and trigger Pistons/Doors/Rails/TNT,
//         but NEVER power adjacent Redstone Wires.
//       * Step-up / step-down diagonal wire diode rule (opaque block above
//         blocks upward climb; opaque block beside blocks downward drop).
//       * 2-game-tick inverter delay + 8-toggles-in-60-ticks Burnout protection
//         withpitch-randomized fizz sound.
//       * Pure Beta 1.7.3 diode + 1..4 redstone-tick (2/4/6/8 game-tick) delay
//         with pulse-lengthening guarantee (no 1.4.2 locking).
//       * Quasi-Connectivity at (x, y + 1, z), 12-block push limit, fragile
//         block crushing/dropping, unmovable block rejection (Obsidian, Bedrock,
//         TileEntities, Extended Pistons), and Sticky Piston retraction.
//       * Exact 16x16x16 ray-marched blast resistance attenuation, TNT chain
//         ignition with 10..29 tick fuse, and 1/power item drop probability.
// ============================================================================

struct PrimedTntEntity {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    int   fuseTicks = 80; // 4 seconds
};

struct RedstoneDelayedEvent {
    int32_t x = 0, y = 0, z = 0;
    int     ticksLeft = 2;
    uint8_t targetId  = 0;
};

struct TorchToggleRecord {
    int32_t  x = 0, y = 0, z = 0;
    uint64_t tick = 0;
};

class BetaRedstonePistonEngine {
public:
    std::vector<PrimedTntEntity>      primedTnts;
    std::vector<RedstoneDelayedEvent> delayedEvents;
    std::deque<TorchToggleRecord>     torchToggleHistory;

    // ========================================================================
    // Player Right-Click Interaction (Levers, Buttons, Repeaters, Doors, Trapdoors)
    // ========================================================================
    template<typename QueueType>
    bool interactWithBlock(WorldMap& world, int bx, int by, int bz, QueueType& meshQueue,
                           AudioEngine* audio = nullptr, const BetaAssetManager* assets = nullptr) {
        uint16_t st  = world.getBlockAt(bx, by, bz);
        uint8_t id   = static_cast<uint8_t>((st >> 4) & 0xFF);
        uint8_t meta = static_cast<uint8_t>(st & 0x0F);

        if (id == 69) { // Lever toggle (BlockLever.blockActivated)
            uint8_t nextMeta = static_cast<uint8_t>(meta ^ 0x08);
            world.setBlockAt(bx, by, bz, static_cast<uint16_t>((69 << 4) | nextMeta), false);
            playClick(audio, assets, (nextMeta & 0x08) ? 0.6f : 0.5f);
            notifyStrongSourceChanged(world, bx, by, bz, getLeverAttachedOffset(meta), meshQueue);
            meshQueue.push({ bx >> 4, bz >> 4 });
            return true;
        }
        if (id == 77) { // Stone Button (BlockButton: 20-tick pulse)
            if ((meta & 0x08) == 0) {
                world.setBlockAt(bx, by, bz, static_cast<uint16_t>((77 << 4) | (meta | 0x08)), false);
                playClick(audio, assets, 0.6f);
                scheduleDelayedUnique(bx, by, bz, 20, 77);
                notifyStrongSourceChanged(world, bx, by, bz, getButtonAttachedOffset(meta), meshQueue);
                meshQueue.push({ bx >> 4, bz >> 4 });
            }
            return true;
        }
        if (id == 93 || id == 94) { // Redstone Repeater cycle delay (1..4 redstone ticks)
            uint8_t dir   = meta & 0x03;
            uint8_t delay = static_cast<uint8_t>(((meta >> 2) + 1) & 0x03);
            world.setBlockAt(bx, by, bz, static_cast<uint16_t>((id << 4) | (delay << 2) | dir), false);
            playClick(audio, assets, 0.55f);
            meshQueue.push({ bx >> 4, bz >> 4 });
            return true;
        }
        if (id == 64) { // Wooden Door toggle (Iron Door 71 ignores hand right-click)
            int baseY = (meta & 0x08) ? (by - 1) : by;
            uint16_t lowSt = world.getBlockAt(bx, baseY, bz);
            uint8_t nextMeta = static_cast<uint8_t>((lowSt & 0x0F) ^ 0x04);
            world.setBlockAt(bx, baseY,     bz, static_cast<uint16_t>((64 << 4) | nextMeta), false);
            world.setBlockAt(bx, baseY + 1, bz, static_cast<uint16_t>((64 << 4) | (nextMeta | 0x08)), false);
            if (audio && assets) {
                const auto& snd = (nextMeta & 0x04) ? assets->doorOpenSound : assets->doorCloseSound;
                if (!snd.samples.empty()) audio->play2D(snd, 0.8f, 1.0f);
            }
            meshQueue.push({ bx >> 4, bz >> 4 });
            return true;
        }
        if (id == 96) { // Trapdoor toggle
            uint8_t nextMeta = static_cast<uint8_t>(meta ^ 0x04);
            world.setBlockAt(bx, by, bz, static_cast<uint16_t>((96 << 4) | nextMeta), false);
            if (audio && assets) {
                const auto& snd = (nextMeta & 0x04) ? assets->doorOpenSound : assets->doorCloseSound;
                if (!snd.samples.empty()) audio->play2D(snd, 0.8f, 1.0f);
            }
            meshQueue.push({ bx >> 4, bz >> 4 });
            return true;
        }
        return false;
    }

    // ========================================================================
    //Direct Strong Power vs. Indirect Weak Power Queries
    // ========================================================================
    // Returns direct strong power (0 or 15) emitted by block (sx, sy, sz) INTO (tx, ty, tz)
    int getDirectPowerToward(const WorldMap& world, int sx, int sy, int sz, int tx, int ty, int tz) const {
        uint16_t st  = world.getBlockAt(sx, sy, sz);
        uint8_t id   = static_cast<uint8_t>((st >> 4) & 0xFF);
        uint8_t meta = static_cast<uint8_t>(st & 0x0F);

        int dx = tx - sx;
        int dy = ty - sy;
        int dz = tz - sz;

        // Redstone Torch On (76): strongly powers all 5 neighbors EXCEPT the block it is mounted on
        if (id == 76) {
            int att[3];
            getTorchAttachedOffset(meta, att);
            if (dx == att[0] && dy == att[1] && dz == att[2]) return 0;
            return 15;
        }

        // Redstone Repeater On (94): strongly powers ONLY the block directly in front of its output
        if (id == 94) {
            int outD[2];
            getRepeaterFrontOffset(meta & 0x03, outD);
            if (dy == 0 && dx == outD[0] && dz == outD[1]) return 15;
            return 0;
        }

        // Active Lever (69): strongly powers all 6 neighbors (including mounted block)
        if (id == 69 && (meta & 0x08)) return 15;

        // Active Button (77): strongly powers all 6 neighbors (including mounted wall block)
        if (id == 77 && (meta & 0x08)) return 15;

        // Active Pressure Plates (70, 72) & Detector Rail (28): strongly power all 6 neighbors (including floor)
        if ((id == 70 || id == 72 || id == 28) && (meta & 0x01)) return 15;

        return 0;
    }

    //World.isBlockGettingPowered: True if solid opaque block at (bx, by, bz) is STRONGLY powered
    bool isBlockStronglyPowered(const WorldMap& world, int bx, int by, int bz) const {
        if (by < 0 || by >= CHUNK_HEIGHT) return false;
        if (!world.registry.get(world.getBlockAt(bx, by, bz)).isOpaque) return false;

        static constexpr int dirs[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
        for (const auto& d : dirs) {
            int sx = bx + d[0], sy = by + d[1], sz = bz + d[2];
            if (getDirectPowerToward(world, sx, sy, sz, bx, by, bz) > 0) {
                return true;
            }
        }
        return false;
    }

    //BlockRedstoneWire.isPoweringTo: True if powered Redstone Wire at (wx, wy, wz) points into (tx, wy, tz)
    bool doesWirePointInto(const WorldMap& world, int wx, int wy, int wz, int tx, int tz) const {
        uint16_t st = world.getBlockAt(wx, wy, wz);
        if (((st >> 4) & 0xFF) != 55 || (st & 0x0F) == 0) return false;

        uint8_t mask = world.registry.getRedstoneWireConnections(wx, wy, wz, [&](int qx, int qy, int qz) {
            return world.getBlockAt(qx, qy, qz);
        });

        // Mask bits: 0 = West(-X), 1 = East(+X), 2 = North(-Z), 3 = South(+Z)
        bool west  = (mask & 0x01) != 0;
        bool east  = (mask & 0x02) != 0;
        bool north = (mask & 0x04) != 0;
        bool south = (mask & 0x08) != 0;

        int dx = tx - wx;
        int dz = tz - wz;
        if (dx == -1 && dz == 0) return !north && !south; // Points West if no N/S branches (includes dot!)
        if (dx ==  1 && dz == 0) return !north && !south; // Points East if no N/S branches
        if (dx ==  0 && dz == -1) return !west && !east;  // Points North if no E/W branches
        if (dx ==  0 && dz ==  1) return !west && !east;  // Points South if no E/W branches
        return false;
    }

    //World.isBlockIndirectlyGettingPowered: True if solid opaque block at (bx, by, bz) is WEAKLY or STRONGLY powered
    bool isBlockWeaklyPowered(const WorldMap& world, int bx, int by, int bz) const {
        if (by < 0 || by >= CHUNK_HEIGHT) return false;
        if (!world.registry.get(world.getBlockAt(bx, by, bz)).isOpaque) return false;

        if (isBlockStronglyPowered(world, bx, by, bz)) return true;

        // Powered Redstone Wire directly on top of this opaque block
        uint16_t aboveSt = world.getBlockAt(bx, by + 1, bz);
        if (((aboveSt >> 4) & 0xFF) == 55 && (aboveSt & 0x0F) > 0) return true;

        // Powered Redstone Wire horizontally adjacent and pointing into this block
        static constexpr int hDirs[4][2] = { {-1,0}, {1,0}, {0,-1}, {0,1} };
        for (const auto& d : hDirs) {
            if (doesWirePointInto(world, bx + d[0], by, bz + d[1], bx, bz)) {
                return true;
            }
        }
        return false;
    }

    // General mechanism power query at (wx, wy, wz) — used by Pistons, Doors, TNT, Powered Rails
    // excludeFace: -1 = check all 6 sides; 0..5 = skip specific BlockFace (e.g. piston front head)
    int getBlockPowerLevel(const WorldMap& world, int wx, int wy, int wz, int excludeFace = -1) const {
        int maxP = 0;
        // Face order: 0=East(+X), 1=West(-X), 2=Top(+Y), 3=Bottom(-Y), 4=South(+Z), 5=North(-Z)
        static constexpr int dirs[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };

        for (int f = 0; f < 6; ++f) {
            if (f == excludeFace) continue;
            int nx = wx + dirs[f][0], ny = wy + dirs[f][1], nz = wz + dirs[f][2];
            if (ny < 0 || ny >= CHUNK_HEIGHT) continue;

            uint16_t st  = world.getBlockAt(nx, ny, nz);
            uint8_t id   = static_cast<uint8_t>((st >> 4) & 0xFF);
            uint8_t meta = static_cast<uint8_t>(st & 0x0F);

            // 1. Direct strong emitter pointing into (wx, wy, wz)
            int direct = getDirectPowerToward(world, nx, ny, nz, wx, wy, wz);
            if (direct > maxP) maxP = direct;

            // 2. Weakly or Strongly powered solid opaque block transmits power to mechanisms
            if (world.registry.getById(id).isOpaque && isBlockWeaklyPowered(world, nx, ny, nz)) {
                maxP = 15;
            }

            // 3. Redstone Wire on top or horizontally pointing into (wx, wy, wz)
            if (id == 55 && meta > 0) {
                if (f == FACE_TOP || (f != FACE_BOTTOM && doesWirePointInto(world, nx, ny, nz, wx, wz))) {
                    if (static_cast<int>(meta) > maxP) maxP = static_cast<int>(meta);
                }
            }
        }
        return maxP;
    }

    // ========================================================================
    // Block Change Propagation & Multi-Pass Redstone Wire Solver
    // ========================================================================
    template<typename QueueType>
    void onBlockChanged(WorldMap& world, int wx, int wy, int wz, QueueType& meshQueue) {
        updateNearbyWireNetworks(world, wx, wy, wz, meshQueue);

        for (int dy = -2; dy <= 2; ++dy) {
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dx = -2; dx <= 2; ++dx) {
                    evaluateComponentAt(world, wx + dx, wy + dy, wz + dz, meshQueue);
                }
            }
        }
    }

    // ========================================================================
    // 20 TPS Redstone Tick (Delayed Torches, Repeaters, Buttons & TNT)
    // ========================================================================
    template<typename QueueType>
    void tick(WorldMap& world, QueueType& meshQueue,
              AudioEngine* audio = nullptr, const BetaAssetManager* assets = nullptr) {
        // 1. Purge old torch burnout records (> 60 ticks old)
        while (!torchToggleHistory.empty() && (world.worldTick - torchToggleHistory.front().tick) > 60ULL) {
            torchToggleHistory.pop_front();
        }

        // 2. Process Delayed Redstone Events (Torches, Repeaters, Buttons)
        size_t evCount = delayedEvents.size();
        for (size_t i = 0; i < evCount; ) {
            if (--delayedEvents[i].ticksLeft > 0) {
                ++i;
                continue;
            }
            RedstoneDelayedEvent ev = delayedEvents[i];
            delayedEvents[i] = delayedEvents.back();
            delayedEvents.pop_back();
            --evCount;

            uint16_t st  = world.getBlockAt(ev.x, ev.y, ev.z);
            uint8_t id   = static_cast<uint8_t>((st >> 4) & 0xFF);
            uint8_t meta = static_cast<uint8_t>(st & 0x0F);

            if (id == 77 && (meta & 0x08)) { // Button release
                world.setBlockAt(ev.x, ev.y, ev.z, static_cast<uint16_t>((77 << 4) | (meta & 0x07)), false);
                playClick(audio, assets, 0.5f);
                notifyStrongSourceChanged(world, ev.x, ev.y, ev.z, getButtonAttachedOffset(meta), meshQueue);
                meshQueue.push({ ev.x >> 4, ev.z >> 4 });
            } else if (id == 75 || id == 76) { // Redstone Torch invert
                bool shouldBeOff = isTorchPoweredByMount(world, ev.x, ev.y, ev.z, meta);
                uint8_t wantId   = shouldBeOff ? 75 : 76;
                if (wantId != id) {
                    if (wantId == 76 && isTorchBurnedOut(world, ev.x, ev.y, ev.z)) {
                        if (audio && assets && !assets->fizzSound.samples.empty()) {
                            float r1 = static_cast<float>((world.worldTick * 1103515245ULL) & 0xFF) / 255.0f;
                            float r2 = static_cast<float>((world.worldTick * 214013ULL) & 0xFF) / 255.0f;
                            audio->play2D(assets->fizzSound, 0.5f, 2.6f + (r1 - r2) * 0.8f);
                        }
                        continue;
                    }
                    torchToggleHistory.push_back({ ev.x, ev.y, ev.z, world.worldTick });
                    world.setBlockAt(ev.x, ev.y, ev.z, static_cast<uint16_t>((wantId << 4) | meta), false);
                    onBlockChanged(world, ev.x, ev.y, ev.z, meshQueue);
                    meshQueue.push({ ev.x >> 4, ev.z >> 4 });
                }
            } else if (id == 93 || id == 94) { // Redstone Repeater diode transition (Beta 1.7.3)
                bool inputPowered = isRepeaterInputPowered(world, ev.x, ev.y, ev.z, meta & 0x03);
                uint8_t wantId    = inputPowered ? 94 : 93;
                if (wantId != id) {
                    world.setBlockAt(ev.x, ev.y, ev.z, static_cast<uint16_t>((wantId << 4) | meta), false);
                    int outD[2];
                    getRepeaterFrontOffset(meta & 0x03, outD);
                    int frontOff[3] = { outD[0], 0, outD[1] };
                    notifyStrongSourceChanged(world, ev.x, ev.y, ev.z, frontOff, meshQueue);
                    meshQueue.push({ ev.x >> 4, ev.z >> 4 });

                    // Pulse-lengthening: if repeater just turned ON while input already turned OFF, schedule turn-off
                    if (wantId == 94 && !inputPowered) {
                        int delayTicks = (((meta >> 2) & 0x03) + 1) * 2;
                        scheduleDelayedUnique(ev.x, ev.y, ev.z, delayTicks, 93);
                    }
                } else if (id == 94 && !inputPowered) {
                    int delayTicks = (((meta >> 2) & 0x03) + 1) * 2;
                    scheduleDelayedUnique(ev.x, ev.y, ev.z, delayTicks, 93);
                }
            }
        }

        // 3. Tick Primed TNT Entities & Detonate (EntityTNTPrimed.onUpdate)
        for (size_t i = 0; i < primedTnts.size(); ) {
            PrimedTntEntity& tnt = primedTnts[i];
            tnt.vy -= 0.04f;
            tnt.x  += tnt.vx;
            tnt.y  += tnt.vy;
            tnt.z  += tnt.vz;
            tnt.vx *= 0.98f; tnt.vy *= 0.98f; tnt.vz *= 0.98f;

            int bx = static_cast<int>(std::floor(tnt.x));
            int by = static_cast<int>(std::floor(tnt.y));
            int bz = static_cast<int>(std::floor(tnt.z));
            if (world.registry.get(world.getBlockAt(bx, by, bz)).isSolid) {
                tnt.y = std::floor(tnt.y) + 1.0f;
                tnt.vy = 0.0f;
                tnt.vx *= 0.7f; tnt.vz *= 0.7f;
            }

            if (--tnt.fuseTicks <= 0) {
                detonateExplosionWithInst(this, world, tnt.x, tnt.y + 0.49f, tnt.z, 4.0f, meshQueue);
                if (audio && assets && !assets->explodeSound.samples.empty()) {
                    audio->play2D(assets->explodeSound, 1.0f, 1.0f);
                }
                primedTnts[i] = primedTnts.back();
                primedTnts.pop_back();
            } else {
                ++i;
            }
        }
    }

    //Explosion.java exact 16x16x16 ray-marched blast resistance destruction
    template<typename QueueType>
    static void detonateExplosion(WorldMap& world, float ex, float ey, float ez, float power, QueueType& meshQueue) {
        detonateExplosionWithInst(nullptr, world, ex, ey, ez, power, meshQueue);
    }

private:
    template<typename QueueType>
    static void detonateExplosionWithInst(BetaRedstonePistonEngine* self, WorldMap& world,
                                          float ex, float ey, float ez, float power, QueueType& meshQueue) {
        std::unordered_set<BlockPosKey, BlockPosHasher> destroyed;
        uint32_t rng = static_cast<uint32_t>(world.worldTick * 1664525ULL + 1013904223ULL) ^
                       static_cast<uint32_t>(std::floor(ex * 31.0f + ez * 17.0f));
        auto nextF = [&]() -> float {
            rng = rng * 1664525u + 1013904223u;
            return static_cast<float>(rng >> 8) / 16777216.0f;
        };

        // 16x16x16 outer cube ray grid (Explosion.doExplosionA)
        for (int i = 0; i < 16; ++i) {
            for (int j = 0; j < 16; ++j) {
                for (int k = 0; k < 16; ++k) {
                    if (i != 0 && i != 15 && j != 0 && j != 15 && k != 0 && k != 15) continue;

                    double dx = static_cast<double>(i) / 15.0 * 2.0 - 1.0;
                    double dy = static_cast<double>(j) / 15.0 * 2.0 - 1.0;
                    double dz = static_cast<double>(k) / 15.0 * 2.0 - 1.0;
                    double len = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (len < 1e-6) continue;
                    dx /= len; dy /= len; dz /= len;

                    float rayPower = power * (0.7f + nextF() * 0.6f);
                    double px = static_cast<double>(ex);
                    double py = static_cast<double>(ey);
                    double pz = static_cast<double>(ez);

                    while (rayPower > 0.0f) {
                        int wx = static_cast<int>(std::floor(px));
                        int wy = static_cast<int>(std::floor(py));
                        int wz = static_cast<int>(std::floor(pz));

                        if (wy >= 0 && wy < CHUNK_HEIGHT) {
                            uint16_t st = world.getBlockAt(wx, wy, wz);
                            if (st != 0) {
                                const BlockDef& def = world.registry.get(st);
                                //stores blockResistance = resistance * 3.0f and divides by 5.0f in Explosion
                                float effectiveRes = (def.resistance * 3.0f) / 5.0f;
                                rayPower -= (effectiveRes + 0.3f) * 0.3f;
                                if (rayPower > 0.0f && def.hardness >= 0.0f) {
                                    destroyed.insert({ wx, wy, wz });
                                }
                            }
                        }

                        px += dx * 0.3;
                        py += dy * 0.3;
                        pz += dz * 0.3;
                        rayPower -= 0.22500001f;
                    }
                }
            }
        }

        for (const BlockPosKey& p : destroyed) {
            if (p.y <= 0 || p.y >= CHUNK_HEIGHT) continue;
            uint16_t st = world.getBlockAt(p.x, p.y, p.z);
            if (st == 0) continue;
            uint8_t id = static_cast<uint8_t>((st >> 4) & 0xFF);

            if (id == 46 && self) {
                // Chain-ignite destroyed TNT with short random fuse (10..29 ticks)
                int shortFuse = 10 + static_cast<int>(nextF() * 20.0f);
                self->primedTnts.push_back({ p.x + 0.5f, static_cast<float>(p.y), p.z + 0.5f, 0.0f, 0.20f, 0.0f, shortFuse });
            } else if (nextF() <= (1.0f / std::max(1.0f, power))) {
                uint16_t dropId = id;
                if (id == 1) dropId = 4;
                else if (id == 2) dropId = 3;
                else if (id == 16) dropId = ITEM_COAL;
                else if (id == 56) dropId = ITEM_DIAMOND;
                else if (id == 73 || id == 74) dropId = ITEM_REDSTONE;
                if (id != 20 && id != 51 && id != 79 && id != 92) {
                    world.pendingDetachedDrops.push_back({ dropId, 0, 1, p.x + 0.5f, p.y + 0.5f, p.z + 0.5f });
                }
            }

            world.setBlockAt(p.x, p.y, p.z, 0, false);
            meshQueue.push({ p.x >> 4, p.z >> 4 });
        }
    }

    static void playClick(AudioEngine* audio, const BetaAssetManager* assets, float pitch) {
        if (audio && assets && !assets->clickSound.samples.empty()) {
            audio->play2D(assets->clickSound, 0.35f, pitch);
        }
    }

    void scheduleDelayedUnique(int32_t x, int32_t y, int32_t z, int ticks, uint8_t targetId) {
        for (const auto& ev : delayedEvents) {
            if (ev.x == x && ev.y == y && ev.z == z) return;
        }
        if (delayedEvents.size() < 4096) {
            delayedEvents.push_back({ x, y, z, ticks, targetId });
        }
    }

    bool isTorchBurnedOut(const WorldMap& world, int32_t x, int32_t y, int32_t z) const {
        int count = 0;
        for (const auto& rec : torchToggleHistory) {
            if (rec.x == x && rec.y == y && rec.z == z && (world.worldTick - rec.tick) <= 60ULL) {
                if (++count >= 8) return true;
            }
        }
        return false;
    }

    static void getTorchAttachedOffset(uint8_t meta, int out[3]) {
        // Torch meta: 1 = attached to West(-X), 2 = East(+X), 3 = North(-Z), 4 = South(+Z), 5/0 = Bottom(-Y)
        uint8_t m = meta & 0x07;
        if (m == 1)      { out[0] = -1; out[1] =  0; out[2] =  0; }
        else if (m == 2) { out[0] =  1; out[1] =  0; out[2] =  0; }
        else if (m == 3) { out[0] =  0; out[1] =  0; out[2] = -1; }
        else if (m == 4) { out[0] =  0; out[1] =  0; out[2] =  1; }
        else             { out[0] =  0; out[1] = -1; out[2] =  0; }
    }

    static std::array<int, 3> getLeverAttachedOffset(uint8_t meta) {
        uint8_t m = meta & 0x07;
        if (m == 1) return { -1,  0,  0 };
        if (m == 2) return {  1,  0,  0 };
        if (m == 3) return {  0,  0, -1 };
        if (m == 4) return {  0,  0,  1 };
        return { 0, -1, 0 };
    }

    static std::array<int, 3> getButtonAttachedOffset(uint8_t meta) {
        uint8_t m = meta & 0x07;
        if (m == 1) return { -1, 0,  0 };
        if (m == 2) return {  1, 0,  0 };
        if (m == 3) return {  0, 0, -1 };
        return { 0, 0, 1 };
    }

    static void getRepeaterFrontOffset(uint8_t dir, int out[2]) {
        // 0 = North(-Z), 1 = East(+X), 2 = South(+Z), 3 = West(-X)
        if (dir == 0)      { out[0] =  0; out[1] = -1; }
        else if (dir == 1) { out[0] =  1; out[1] =  0; }
        else if (dir == 2) { out[0] =  0; out[1] =  1; }
        else               { out[0] = -1; out[1] =  0; }
    }

    bool isTorchPoweredByMount(const WorldMap& world, int tx, int ty, int tz, uint8_t meta) const {
        int att[3];
        getTorchAttachedOffset(meta, att);
        int mx = tx + att[0], my = ty + att[1], mz = tz + att[2];
        if (world.registry.get(world.getBlockAt(mx, my, mz)).isOpaque) {
            return isBlockWeaklyPowered(world, mx, my, mz);
        }
        return getBlockPowerLevel(world, mx, my, mz) > 0;
    }

    bool isRepeaterInputPowered(const WorldMap& world, int rx, int ry, int rz, uint8_t dir) const {
        int front[2];
        getRepeaterFrontOffset(dir, front);
        int ix = rx - front[0];
        int iz = rz - front[1];

        uint16_t inSt  = world.getBlockAt(ix, ry, iz);
        uint8_t inId   = static_cast<uint8_t>((inSt >> 4) & 0xFF);
        uint8_t inMeta = static_cast<uint8_t>(inSt & 0x0F);

        if (inId == 55 && inMeta > 0) return true;
        if (getDirectPowerToward(world, ix, ry, iz, rx, ry, rz) > 0) return true;
        if (world.registry.getById(inId).isOpaque && isBlockWeaklyPowered(world, ix, ry, iz)) return true;
        return false;
    }

    template<typename QueueType>
    void notifyStrongSourceChanged(WorldMap& world, int sx, int sy, int sz,
                                   const std::array<int, 3>& strongTargetOff, QueueType& meshQueue) {
        onBlockChanged(world, sx, sy, sz, meshQueue);
        int tx = sx + strongTargetOff[0];
        int ty = sy + strongTargetOff[1];
        int tz = sz + strongTargetOff[2];
        onBlockChanged(world, tx, ty, tz, meshQueue);
    }

    template<typename QueueType>
    void notifyStrongSourceChanged(WorldMap& world, int sx, int sy, int sz,
                                   const int strongTargetOff[3], QueueType& meshQueue) {
        std::array<int, 3> arr{ strongTargetOff[0], strongTargetOff[1], strongTargetOff[2] };
        notifyStrongSourceChanged(world, sx, sy, sz, arr, meshQueue);
    }

    // ========================================================================
    // Iterative Multi-Pass Redstone Wire Solver (BlockRedstoneWire.calculateCurrentChanges)
    // ========================================================================
    template<typename QueueType>
    void updateNearbyWireNetworks(WorldMap& world, int cx, int cy, int cz, QueueType& meshQueue) {
        std::vector<BlockPosKey> network;
        std::unordered_set<BlockPosKey, BlockPosHasher> visited;
        network.reserve(128);

        auto tryEnqueueWire = [&](int wx, int wy, int wz) {
            if (wy < 0 || wy >= CHUNK_HEIGHT) return;
            if (((world.getBlockAt(wx, wy, wz) >> 4) & 0xFF) != 55) return;
            BlockPosKey k{ wx, wy, wz };
            if (visited.insert(k).second && network.size() < 512) {
                network.push_back(k);
            }
        };

        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx)
                    tryEnqueueWire(cx + dx, cy + dy, cz + dz);

        if (network.empty()) return;

        // Expand connected wire graph across horizontal, step-up, and step-down edges
        static constexpr int hDirs[4][2] = { {-1,0}, {1,0}, {0,-1}, {0,1} };
        for (size_t idx = 0; idx < network.size(); ++idx) {
            BlockPosKey cur = network[idx];
            bool aboveOpaque = world.registry.get(world.getBlockAt(cur.x, cur.y + 1, cur.z)).isOpaque;
            for (const auto& d : hDirs) {
                int nx = cur.x + d[0], nz = cur.z + d[1];
                tryEnqueueWire(nx, cur.y, nz);
                if (!aboveOpaque) tryEnqueueWire(nx, cur.y + 1, nz);
                if (!world.registry.get(world.getBlockAt(nx, cur.y, nz)).isOpaque) {
                    tryEnqueueWire(nx, cur.y - 1, nz);
                }
            }
        }

        // Compute external strong power feeding each wire node
        std::vector<uint8_t> solvedPower(network.size(), 0);
        static constexpr int dirs6[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };

        for (size_t i = 0; i < network.size(); ++i) {
            BlockPosKey p = network[i];
            int ext = 0;
            for (const auto& d : dirs6) {
                int sx = p.x + d[0], sy = p.y + d[1], sz = p.z + d[2];
                if (sy < 0 || sy >= CHUNK_HEIGHT) continue;
                uint16_t sSt = world.getBlockAt(sx, sy, sz);
                uint8_t  sId = static_cast<uint8_t>((sSt >> 4) & 0xFF);
                if (sId == 55) continue;

                int direct = getDirectPowerToward(world, sx, sy, sz, p.x, p.y, p.z);
                if (direct > ext) ext = direct;

                // Only STRONGLY powered solid opaque blocks inject power into Redstone Wire
                if (world.registry.getById(sId).isOpaque && isBlockStronglyPowered(world, sx, sy, sz)) {
                    ext = 15;
                }
            }
            solvedPower[i] = static_cast<uint8_t>(ext);
        }

        // Relax signal strength across edges (15-step max propagation)
        bool changed = true;
        for (int iter = 0; iter < 16 && changed; ++iter) {
            changed = false;
            for (size_t i = 0; i < network.size(); ++i) {
                if (solvedPower[i] <= 1) continue;
                uint8_t nextL = static_cast<uint8_t>(solvedPower[i] - 1);
                BlockPosKey cur = network[i];
                bool aboveOpaque = world.registry.get(world.getBlockAt(cur.x, cur.y + 1, cur.z)).isOpaque;

                auto relaxNeighbor = [&](int qx, int qy, int qz) {
                    for (size_t j = 0; j < network.size(); ++j) {
                        if (network[j].x == qx && network[j].y == qy && network[j].z == qz) {
                            if (nextL > solvedPower[j]) {
                                solvedPower[j] = nextL;
                                changed = true;
                            }
                            break;
                        }
                    }
                };

                for (const auto& d : hDirs) {
                    int nx = cur.x + d[0], nz = cur.z + d[1];
                    relaxNeighbor(nx, cur.y, nz);
                    if (!aboveOpaque) relaxNeighbor(nx, cur.y + 1, nz);
                    if (!world.registry.get(world.getBlockAt(nx, cur.y, nz)).isOpaque) {
                        relaxNeighbor(nx, cur.y - 1, nz);
                    }
                }
            }
        }

        // Commit updated metadata & trigger mechanism updates around changed wires
        for (size_t i = 0; i < network.size(); ++i) {
            BlockPosKey p = network[i];
            uint16_t curSt = world.getBlockAt(p.x, p.y, p.z);
            uint8_t curP   = static_cast<uint8_t>(curSt & 0x0F);
            if (curP != solvedPower[i]) {
                world.setBlockAt(p.x, p.y, p.z, static_cast<uint16_t>((55 << 4) | (solvedPower[i] & 0x0F)), false);
                meshQueue.push({ p.x >> 4, p.z >> 4 });

                for (int dy = -2; dy <= 1; ++dy) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            evaluateComponentAt(world, p.x + dx, p.y + dy, p.z + dz, meshQueue);
                        }
                    }
                }
            }
        }
    }

    template<typename QueueType>
    void evaluateComponentAt(WorldMap& world, int nx, int ny, int nz, QueueType& meshQueue) {
        if (ny < 0 || ny >= CHUNK_HEIGHT) return;
        uint16_t st  = world.getBlockAt(nx, ny, nz);
        uint8_t id   = static_cast<uint8_t>((st >> 4) & 0xFF);
        uint8_t meta = static_cast<uint8_t>(st & 0x0F);

        if (id == 75 || id == 76) { // Redstone Torch inverter
            bool shouldBeOff = isTorchPoweredByMount(world, nx, ny, nz, meta);
            uint8_t wantId   = shouldBeOff ? 75 : 76;
            if (wantId != id) {
                scheduleDelayedUnique(nx, ny, nz, 2, wantId);
            }
        } else if (id == 93 || id == 94) { // Redstone Repeater
            bool inputPowered = isRepeaterInputPowered(world, nx, ny, nz, meta & 0x03);
            uint8_t wantId    = inputPowered ? 94 : 93;
            if (wantId != id) {
                int delayTicks = (((meta >> 2) & 0x03) + 1) * 2;
                scheduleDelayedUnique(nx, ny, nz, delayTicks, wantId);
            }
        } else if (id == 29 || id == 33) { // Sticky Piston (29) & Piston (33)
            updatePistonState(world, nx, ny, nz, st, meshQueue);
        } else if (id == 46 && getBlockPowerLevel(world, nx, ny, nz) > 0) { // TNT ignition
            world.setBlockAt(nx, ny, nz, 0, true);
            primedTnts.push_back({ nx + 0.5f, static_cast<float>(ny), nz + 0.5f, 0.0f, 0.20f, 0.0f, 80 });
            meshQueue.push({ nx >> 4, nz >> 4 });
        } else if ((id == 64 || id == 71) && (meta & 0x08) == 0) { // Wooden (64) & Iron (71) Door lower half
            bool powered = (getBlockPowerLevel(world, nx, ny, nz) > 0) || (getBlockPowerLevel(world, nx, ny + 1, nz) > 0);
            bool isOpen  = (meta & 0x04) != 0;
            if (powered != isOpen) {
                uint8_t nextMeta = powered ? (meta | 0x04) : (meta & ~0x04);
                world.setBlockAt(nx, ny,     nz, static_cast<uint16_t>((id << 4) | nextMeta), false);
                world.setBlockAt(nx, ny + 1, nz, static_cast<uint16_t>((id << 4) | (nextMeta | 0x08)), false);
                meshQueue.push({ nx >> 4, nz >> 4 });
            }
        } else if (id == 96) { // Trapdoor
            bool powered = getBlockPowerLevel(world, nx, ny, nz) > 0;
            bool isOpen  = (meta & 0x04) != 0;
            if (powered != isOpen) {
                uint8_t nextMeta = powered ? (meta | 0x04) : (meta & ~0x04);
                world.setBlockAt(nx, ny, nz, static_cast<uint16_t>((96 << 4) | nextMeta), false);
                meshQueue.push({ nx >> 4, nz >> 4 });
            }
        } else if (id == 27) { // Powered Rail (propagates up to 8 connected rails)
            bool powered = (getBlockPowerLevel(world, nx, ny, nz) > 0) || isPoweredRailChainActive(world, nx, ny, nz, meta & 0x07, 0);
            bool isOn    = (meta & 0x08) != 0;
            if (powered != isOn) {
                uint8_t nextMeta = powered ? (meta | 0x08) : (meta & 0x07);
                world.setBlockAt(nx, ny, nz, static_cast<uint16_t>((27 << 4) | nextMeta), false);
                meshQueue.push({ nx >> 4, nz >> 4 });

                static constexpr int rDirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
                for (const auto& d : rDirs) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        if (((world.getBlockAt(nx + d[0], ny + dy, nz + d[1]) >> 4) & 0xFF) == 27) {
                            evaluateComponentAt(world, nx + d[0], ny + dy, nz + d[1], meshQueue);
                        }
                    }
                }
            }
        }
    }

    bool isPoweredRailChainActive(const WorldMap& world, int rx, int ry, int rz, uint8_t shape, int depth) const {
        if (depth >= 8) return false;
        int dx = (shape == 1 || shape == 2 || shape == 3) ? 1 : 0;
        int dz = (shape == 0 || shape == 4 || shape == 5) ? 1 : 0;
        if (dx == 0 && dz == 0) return false;

        for (int sign = -1; sign <= 1; sign += 2) {
            for (int dy = -1; dy <= 1; ++dy) {
                int nx = rx + dx * sign, ny = ry + dy, nz = rz + dz * sign;
                uint16_t st = world.getBlockAt(nx, ny, nz);
                if (((st >> 4) & 0xFF) == 27) {
                    uint8_t nShape = st & 0x07;
                    if (nShape == shape) {
                        if (getBlockPowerLevel(world, nx, ny, nz) > 0) return true;
                        if (isPoweredRailChainActive(world, nx, ny, nz, nShape, depth + 1)) return true;
                    }
                }
            }
        }
        return false;
    }

    // ========================================================================
    //BlockPistonBase.java (Quasi-Connectivity + 12-Block Push + Fragile Pop)
    // ========================================================================
    static bool canBlockBePushedByPiston(const WorldMap& world, uint16_t st) {
        uint8_t id   = static_cast<uint8_t>((st >> 4) & 0xFF);
        uint8_t meta = static_cast<uint8_t>(st & 0x0F);
        if (id == 0) return true;
        // Unmovable blocks in Beta 1.7.3
        if (id == 7 || id == 49 || id == 34 || id == 36 || id == 52 || id == 90) return false;
        // Extended pistons cannot be pushed
        if ((id == 29 || id == 33) && (meta & 0x08) != 0) return false;
        // Blocks with TileEntities cannot be pushed in Beta 1.7.3
        if (id == 54 || id == 61 || id == 62 || id == 63 || id == 68 || id == 23 || id == 25 || id == 84) return false;

        const BlockDef& def = world.registry.getById(id);
        return def.hardness >= 0.0f && def.resistance < 100.0f;
    }

    static bool isFragileCrushedByPiston(uint8_t id) {
        return id == 6  || id == 26 || id == 27 || id == 28 || id == 30 || id == 31 || id == 32 ||
               id == 37 || id == 38 || id == 39 || id == 40 || id == 50 || id == 55 || id == 59 ||
               id == 64 || id == 65 || id == 66 || id == 69 || id == 70 || id == 71 || id == 72 ||
               id == 75 || id == 76 || id == 77 || id == 78 || id == 81 || id == 83 || id == 86 ||
               id == 91 || id == 92 || id == 93 || id == 94 || id == 96;
    }

    bool isPistonPowered(const WorldMap& world, int px, int py, int pz, uint8_t dir) const {
        // Piston dir: 0=Down(-Y), 1=Up(+Y), 2=North(-Z), 3=South(+Z), 4=West(-X), 5=East(+X)
        // BlockFace:  0=East(+X), 1=West(-X), 2=Top(+Y), 3=Bottom(-Y), 4=South(+Z), 5=North(-Z)
        static constexpr int dirToFace[6] = { FACE_BOTTOM, FACE_TOP, FACE_NORTH, FACE_SOUTH, FACE_WEST, FACE_EAST };
        int headFace = dirToFace[dir & 0x07];

        // 1. Direct neighbors excluding the front piston head face
        if (getBlockPowerLevel(world, px, py, pz, headFace) > 0) return true;

        // 2.Quasi-Connectivity: check block at (px, py + 1, pz) excluding bottom face (the piston itself)
        if (py + 1 < CHUNK_HEIGHT) {
            if (getBlockPowerLevel(world, px, py + 1, pz, FACE_BOTTOM) > 0) return true;
        }
        return false;
    }

    template<typename QueueType>
    void updatePistonState(WorldMap& world, int px, int py, int pz, uint16_t st, QueueType& meshQueue) {
        uint8_t id       = static_cast<uint8_t>((st >> 4) & 0xFF);
        uint8_t meta     = static_cast<uint8_t>(st & 0x0F);
        uint8_t dir      = meta & 0x07;
        bool isSticky    = (id == 29);
        bool isExtended  = (meta & 0x08) != 0;
        bool shouldPower = isPistonPowered(world, px, py, pz, dir);

        static constexpr int dirVec[6][3] = { {0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0} };
        int vx = dirVec[dir][0], vy = dirVec[dir][1], vz = dirVec[dir][2];

        if (shouldPower && !isExtended) {
            // Count pushable chain up to 12 blocks (Mojang BlockPistonBase.canExtend)
            int pushCount = 0;
            for (int step = 1; step <= 13; ++step) {
                int tx = px + vx * step, ty = py + vy * step, tz = pz + vz * step;
                if (ty < 1 || ty >= CHUNK_HEIGHT - 1) return;
                uint16_t tst = world.getBlockAt(tx, ty, tz);
                uint8_t  tid = static_cast<uint8_t>((tst >> 4) & 0xFF);
                if (tid == 0 || world.registry.getById(tid).isReplaceable || isFragileCrushedByPiston(tid)) {
                    break;
                }
                if (!canBlockBePushedByPiston(world, tst)) return;
                pushCount++;
                if (pushCount > 12) return;
            }

            // Crush/drop fragile block at the end of the push chain if present
            int endX = px + vx * (pushCount + 1);
            int endY = py + vy * (pushCount + 1);
            int endZ = pz + vz * (pushCount + 1);
            uint16_t endSt = world.getBlockAt(endX, endY, endZ);
            uint8_t  endId = static_cast<uint8_t>((endSt >> 4) & 0xFF);
            if (endId != 0 && isFragileCrushedByPiston(endId)) {
                uint16_t dropId = endId;
                if (endId == 55) dropId = ITEM_REDSTONE;
                else if (endId == 93 || endId == 94) dropId = ITEM_REPEATER_ITEM;
                else if (endId == 83) dropId = ITEM_REEDS;
                else if (endId == 64) dropId = ITEM_WOOD_DOOR;
                else if (endId == 71) dropId = ITEM_IRON_DOOR;
                else if (endId == 26) dropId = ITEM_BED_ITEM;
                else if (endId == 59) dropId = ITEM_SEEDS;
                else if (endId == 78) dropId = ITEM_SNOWBALL;
                if (endId != 92 && endId != 30 && endId != 51) {
                    world.pendingDetachedDrops.push_back({ dropId, 0, 1, endX + 0.5f, endY + 0.3f, endZ + 0.5f });
                }
                world.setBlockAt(endX, endY, endZ, 0, false);
            }

            // Shift pushed blocks forward
            for (int step = pushCount; step >= 1; --step) {
                int sx = px + vx * step, sy = py + vy * step, sz = pz + vz * step;
                uint16_t moveSt = world.getBlockAt(sx, sy, sz);
                world.setBlockAt(sx + vx, sy + vy, sz + vz, moveSt, false);
                meshQueue.push({ (sx + vx) >> 4, (sz + vz) >> 4 });
            }

            uint8_t headMeta = static_cast<uint8_t>(dir | (isSticky ? 0x08 : 0x00));
            world.setBlockAt(px + vx, py + vy, pz + vz, static_cast<uint16_t>((34 << 4) | headMeta), false);
            world.setBlockAt(px, py, pz, static_cast<uint16_t>((id << 4) | (meta | 0x08)), false);
            meshQueue.push({ px >> 4, pz >> 4 });
            meshQueue.push({ (px + vx) >> 4, (pz + vz) >> 4 });
        } else if (!shouldPower && isExtended) {
            int hx = px + vx, hy = py + vy, hz = pz + vz;
            if (((world.getBlockAt(hx, hy, hz) >> 4) & 0xFF) == 34) {
                world.setBlockAt(hx, hy, hz, 0, false);
            }
            if (isSticky) {
                int grabX = px + vx * 2, grabY = py + vy * 2, grabZ = pz + vz * 2;
                if (grabY >= 1 && grabY < CHUNK_HEIGHT - 1) {
                    uint16_t grabSt = world.getBlockAt(grabX, grabY, grabZ);
                    uint8_t  grabId = static_cast<uint8_t>((grabSt >> 4) & 0xFF);
                    if (grabId != 0 && !world.registry.getById(grabId).isReplaceable &&
                        !isFragileCrushedByPiston(grabId) && canBlockBePushedByPiston(world, grabSt)) {
                        world.setBlockAt(hx, hy, hz, grabSt, false);
                        world.setBlockAt(grabX, grabY, grabZ, 0, false);
                        meshQueue.push({ grabX >> 4, grabZ >> 4 });
                    }
                }
            }
            world.setBlockAt(px, py, pz, static_cast<uint16_t>((id << 4) | (meta & 0x07)), false);
            meshQueue.push({ px >> 4, pz >> 4 });
            meshQueue.push({ hx >> 4, hz >> 4 });
        }
    }
};