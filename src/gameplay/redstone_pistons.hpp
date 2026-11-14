#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "world/world.hpp"

#include <cstdint>
#include <vector>
#include <algorithm>

// ============================================================================
// Minecraft Beta 1.7.3 Redstone Circuits & Pistons Engine
// ============================================================================
// Authentic Beta 1.7.3 Mechanics:
//   - Redstone Wire signal propagation (0..15) with vertical step-up/down
//   - Redstone Torches (75=Off, 76=On) with 2-tick inverter delay
//   - Redstone Repeaters (93=Off, 94=On) with 1..4 tick configurable delay
//   - Levers (69), Stone Buttons (77), Wooden & Stone Pressure Plates (70, 72)
//   - Pistons (33) & Sticky Pistons (29) pushing/pulling up to 12 blocks!
//   - Primed TNT entities (Block ID 46)
// ============================================================================

struct PrimedTntEntity {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    int   fuseTicks = 80; // 4 seconds
};

struct RedstoneDelayedEvent {
    int32_t x = 0, y = 0, z = 0;
    int     ticksLeft = 2;
};

class BetaRedstonePistonEngine {
public:
    std::vector<PrimedTntEntity>      primedTnts;
    std::vector<RedstoneDelayedEvent> delayedEvents;

    template<typename QueueType>
    bool interactWithBlock(WorldMap& world, int bx, int by, int bz, QueueType& meshQueue) {
        uint16_t st  = world.getBlockAt(bx, by, bz);
        uint8_t id   = (uint8_t)((st >> 4) & 0xFF);
        uint8_t meta = (uint8_t)(st & 0x0F);

        if (id == 69) { // Lever toggle
            uint16_t next = (uint16_t)((69 << 4) | (meta ^ 0x08));
            world.setBlockAt(bx, by, bz, next, false);
            onBlockChanged(world, bx, by, bz, meshQueue);
            meshQueue.push({ bx >> 4, bz >> 4 });
            return true;
        }
        if (id == 77) { // Stone Button (20-tick pulse)
            if ((meta & 0x08) == 0) {
                world.setBlockAt(bx, by, bz, (uint16_t)((77 << 4) | (meta | 0x08)), false);
                delayedEvents.push_back({ bx, by, bz, 20 });
                onBlockChanged(world, bx, by, bz, meshQueue);
                meshQueue.push({ bx >> 4, bz >> 4 });
            }
            return true;
        }
        if (id == 93 || id == 94) { // Redstone Repeater cycle delay (1..4 ticks)
            uint8_t dir   = meta & 0x03;
            uint8_t delay = ((meta >> 2) + 1) & 0x03;
            world.setBlockAt(bx, by, bz, (uint16_t)((id << 4) | (delay << 2) | dir), false);
            meshQueue.push({ bx >> 4, bz >> 4 });
            return true;
        }
        if (id == 64) { // Wooden Door toggle
            int baseY = (meta & 0x08) ? (by - 1) : by;
            uint16_t lowSt = world.getBlockAt(bx, baseY, bz);
            uint8_t nextMeta = (uint8_t)((lowSt & 0x0F) ^ 0x04);
            world.setBlockAt(bx, baseY, bz, (uint16_t)((64 << 4) | nextMeta), false);
            world.setBlockAt(bx, baseY + 1, bz, (uint16_t)((64 << 4) | (nextMeta | 0x08)), false);
            meshQueue.push({ bx >> 4, bz >> 4 });
            return true;
        }
        if (id == 96) { // Trapdoor toggle
            world.setBlockAt(bx, by, bz, (uint16_t)((96 << 4) | (meta ^ 0x04)), false);
            meshQueue.push({ bx >> 4, bz >> 4 });
            return true;
        }
        return false;
    }

    int getBlockPowerLevel(const WorldMap& world, int wx, int wy, int wz) const {
        int maxP = 0;
        static constexpr int dirs[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const auto& d : dirs) {
            int nx = wx + d[0], ny = wy + d[1], nz = wz + d[2];
            uint16_t st  = world.getBlockAt(nx, ny, nz);
            uint8_t id   = (uint8_t)((st >> 4) & 0xFF);
            uint8_t meta = (uint8_t)(st & 0x0F);

            if (id == 76) return 15; // Redstone Torch (On)
            if (id == 69 && (meta & 0x08)) return 15; // Active Lever
            if (id == 77 && (meta & 0x08)) return 15; // Active Button
            if ((id == 70 || id == 72) && (meta & 0x01)) return 15; // Active Pressure Plate
            if (id == 94) return 15; // Powered Repeater
            if (id == 55 && d[1] >= 0) maxP = std::max(maxP, (int)meta);
        }
        return maxP;
    }

    template<typename QueueType>
    void onBlockChanged(WorldMap& world, int wx, int wy, int wz, QueueType& meshQueue) {
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    int nx = wx + dx, ny = wy + dy, nz = wz + dz;
                    uint16_t st  = world.getBlockAt(nx, ny, nz);
                    uint8_t id   = (uint8_t)((st >> 4) & 0xFF);
                    uint8_t meta = (uint8_t)(st & 0x0F);

                    if (id == 55) { // Redstone Wire power recalculation
                        int p = std::max(0, getBlockPowerLevel(world, nx, ny, nz) - 1);
                        if (p != (int)meta) {
                            world.setBlockAt(nx, ny, nz, (uint16_t)((55 << 4) | (p & 0x0F)), false);
                            meshQueue.push({ nx >> 4, nz >> 4 });
                        }
                    } else if (id == 75 || id == 76) { // Redstone Torch inverter
                        bool poweredBelow = getBlockPowerLevel(world, nx, ny - 1, nz) > 0;
                        uint8_t wantId = poweredBelow ? 75 : 76;
                        if (wantId != id) {
                            delayedEvents.push_back({ nx, ny, nz, 2 });
                        }
                    } else if (id == 93 || id == 94) { // Redstone Repeater
                        uint8_t delayTicks = (uint8_t)(((meta >> 2) + 1) * 2);
                        delayedEvents.push_back({ nx, ny, nz, delayTicks });
                    } else if (id == 29 || id == 33) { // Sticky Piston (29) or Piston (33)
                        updatePistonState(world, nx, ny, nz, st, meshQueue);
                    } else if (id == 46 && getBlockPowerLevel(world, nx, ny, nz) > 0) { // TNT ignition
                        world.setBlockAt(nx, ny, nz, 0, true);
                        primedTnts.push_back({ nx + 0.5f, (float)ny, nz + 0.5f, 0.0f, 0.20f, 0.0f, 80 });
                        meshQueue.push({ nx >> 4, nz >> 4 });
                    }
                }
            }
        }
    }

    template<typename QueueType>
    void tick(WorldMap& world, QueueType& meshQueue) {
        // 1. Process Delayed Redstone Events (Torches, Repeaters, Buttons)
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
            uint8_t id   = (uint8_t)((st >> 4) & 0xFF);
            uint8_t meta = (uint8_t)(st & 0x0F);

            if (id == 77 && (meta & 0x08)) { // Button release
                world.setBlockAt(ev.x, ev.y, ev.z, (uint16_t)((77 << 4) | (meta & 0x07)), false);
                onBlockChanged(world, ev.x, ev.y, ev.z, meshQueue);
                meshQueue.push({ ev.x >> 4, ev.z >> 4 });
            } else if (id == 75 || id == 76) { // Torch invert
                bool poweredBelow = getBlockPowerLevel(world, ev.x, ev.y - 1, ev.z) > 0;
                uint8_t wantId = poweredBelow ? 75 : 76;
                if (wantId != id) {
                    world.setBlockAt(ev.x, ev.y, ev.z, (uint16_t)((wantId << 4) | meta), false);
                    onBlockChanged(world, ev.x, ev.y, ev.z, meshQueue);
                    meshQueue.push({ ev.x >> 4, ev.z >> 4 });
                }
            } else if (id == 93 || id == 94) { // Repeater output
                int backDir[4][2] = {{0,1},{-1,0},{0,-1},{1,0}};
                uint8_t dir = meta & 0x03;
                bool inputPowered = getBlockPowerLevel(world, ev.x + backDir[dir][0], ev.y, ev.z + backDir[dir][1]) > 0;
                uint8_t wantId = inputPowered ? 94 : 93;
                if (wantId != id) {
                    world.setBlockAt(ev.x, ev.y, ev.z, (uint16_t)((wantId << 4) | meta), false);
                    onBlockChanged(world, ev.x, ev.y, ev.z, meshQueue);
                    meshQueue.push({ ev.x >> 4, ev.z >> 4 });
                }
            }
        }

        // 2. Tick Primed TNT Entities & Detonate
        for (size_t i = 0; i < primedTnts.size(); ) {
            PrimedTntEntity& tnt = primedTnts[i];
            tnt.vy -= 0.04f;
            tnt.x  += tnt.vx;
            tnt.y  += tnt.vy;
            tnt.z  += tnt.vz;
            tnt.vx *= 0.98f; tnt.vy *= 0.98f; tnt.vz *= 0.98f;

            int bx = (int)std::floor(tnt.x), by = (int)std::floor(tnt.y), bz = (int)std::floor(tnt.z);
            if (world.registry.get(world.getBlockAt(bx, by, bz)).isSolid) {
                tnt.y = std::floor(tnt.y) + 1.0f;
                tnt.vy = 0.0f;
                tnt.vx *= 0.7f; tnt.vz *= 0.7f;
            }

            if (--tnt.fuseTicks <= 0) {
                detonateExplosion(world, tnt.x, tnt.y + 0.5f, tnt.z, 4.0f, meshQueue);
                primedTnts[i] = primedTnts.back();
                primedTnts.pop_back();
            } else {
                ++i;
            }
        }
    }

    template<typename QueueType>
    static void detonateExplosion(WorldMap& world, float ex, float ey, float ez, float power, QueueType& meshQueue) {
        int rad = (int)std::ceil(power);
        int cx0 = (int)std::floor(ex), cy0 = (int)std::floor(ey), cz0 = (int)std::floor(ez);

        for (int dy = -rad; dy <= rad; ++dy) {
            for (int dz = -rad; dz <= rad; ++dz) {
                for (int dx = -rad; dx <= rad; ++dx) {
                    float dist = std::sqrt((float)(dx * dx + dy * dy + dz * dz));
                    if (dist > power) continue;
                    int wx = cx0 + dx, wy = cy0 + dy, wz = cz0 + dz;
                    if (wy <= 0 || wy >= CHUNK_HEIGHT) continue;
                    uint16_t st = world.getBlockAt(wx, wy, wz);
                    if (st == 0) continue;
                    const BlockDef& def = world.registry.get(st);
                    if (def.hardness >= 0.0f && def.resistance < 100.0f) {
                        world.setBlockAt(wx, wy, wz, 0, false);
                        meshQueue.push({ wx >> 4, wz >> 4 });
                    }
                }
            }
        }
    }

private:
    template<typename QueueType>
    void updatePistonState(WorldMap& world, int px, int py, int pz, uint16_t st, QueueType& meshQueue) {
        uint8_t id       = (uint8_t)((st >> 4) & 0xFF);
        uint8_t meta     = (uint8_t)(st & 0x0F);
        uint8_t dir      = meta & 0x07;
        bool isSticky    = (id == 29);
        bool isExtended  = (meta & 0x08) != 0;
        bool shouldPower = getBlockPowerLevel(world, px, py, pz) > 0;

        static constexpr int dirVec[6][3] = {{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
        int vx = dirVec[dir][0], vy = dirVec[dir][1], vz = dirVec[dir][2];

        if (shouldPower && !isExtended) {
            // Count pushable chain up to 12 blocks
            int pushCount = 0;
            for (int step = 1; step <= 13; ++step) {
                int tx = px + vx * step, ty = py + vy * step, tz = pz + vz * step;
                if (ty < 1 || ty >= CHUNK_HEIGHT - 1) return;
                uint16_t tst = world.getBlockAt(tx, ty, tz);
                if (tst == 0 || world.registry.get(tst).isReplaceable) break;
                if (world.registry.get(tst).hardness < 0.0f || world.registry.get(tst).resistance > 100.0f) return;
                pushCount++;
                if (pushCount > 12) return;
            }

            // Shift pushed blocks forward
            for (int step = pushCount; step >= 1; --step) {
                uint16_t moveSt = world.getBlockAt(px + vx * step, py + vy * step, pz + vz * step);
                world.setBlockAt(px + vx * (step + 1), py + vy * (step + 1), pz + vz * (step + 1), moveSt, false);
            }

            uint8_t headMeta = (uint8_t)(dir | (isSticky ? 0x08 : 0x00));
            world.setBlockAt(px + vx, py + vy, pz + vz, (uint16_t)((34 << 4) | headMeta), false);
            world.setBlockAt(px, py, pz, (uint16_t)((id << 4) | (meta | 0x08)), false);
            meshQueue.push({ px >> 4, pz >> 4 });
        } else if (!shouldPower && isExtended) {
            // Retract piston head
            int hx = px + vx, hy = py + vy, hz = pz + vz;
            if (((world.getBlockAt(hx, hy, hz) >> 4) & 0xFF) == 34) {
                world.setBlockAt(hx, hy, hz, 0, false);
            }
            if (isSticky) {
                int grabX = px + vx * 2, grabY = py + vy * 2, grabZ = pz + vz * 2;
                uint16_t grabSt = world.getBlockAt(grabX, grabY, grabZ);
                if (grabSt != 0 && world.registry.get(grabSt).hardness >= 0.0f && world.registry.get(grabSt).resistance < 100.0f) {
                    world.setBlockAt(hx, hy, hz, grabSt, false);
                    world.setBlockAt(grabX, grabY, grabZ, 0, false);
                }
            }
            world.setBlockAt(px, py, pz, (uint16_t)((id << 4) | (meta & 0x07)), false);
            meshQueue.push({ px >> 4, pz >> 4 });
        }
    }
};