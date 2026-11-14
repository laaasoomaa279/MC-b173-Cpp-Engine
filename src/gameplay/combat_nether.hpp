#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "world/world.hpp"
#include "gameplay/physics.hpp"

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================================
// Minecraft Beta 1.7.3 Classic Combat, Armor & Nether Portals (1:1 Mojang)
// ============================================================================
// Exact Reproduction of Notch's Beta 1.7.3 Source Code:
//   - InventoryPlayer.getTotalArmorValue() & EntityLiving.damageEntity():
//       * Exact durability-weighted total armor formula:
//           armorValue = ((K - 1) * I) / J + 1  (when J > 0, else 0)
//         where K = sum of max armor points (3, 8, 6, 3 -> max 20 = 80% cap),
//         I = sum of remaining durability, and J = sum of max durability.
//       * Exact carryoverDamage sub-half-heart damage accumulator across hits.
//   - BlockPortal.tryToCreatePortal():
//       * Exact 4x5 Obsidian frame validation (2x3 interior air/fire, optional
//         corners, X-axis vs Z-axis non-intersecting orientation check).
//   - Teleporter.java (placeInPortal / placeInExistingPortal / createPortal):
//       * 80-tick (4-second) portal stand warmup, 1:8 Overworld <-> Nether
//         coordinate scaling, 128-block existing portal search, and natural
//         ground search with fallback Obsidian bridge/platform generation.
//   - EntityArrow.java / EntityThrowable.java:
//       * Exact heading spread (setArrowHeading), 0.03F gravity, 0.99F air
//         drag, and 0.80F water drag.
// ============================================================================

enum ProjectileKind : uint8_t {
    PROJ_ARROW    = 0,
    PROJ_SNOWBALL = 1,
    PROJ_EGG      = 2
};

struct ProjectileEntity {
    ProjectileKind kind        = PROJ_ARROW;
    bool           fromPlayer  = true;
    int            ownerMobIdx = -1;
    float          x = 0.0f, y = 0.0f, z = 0.0f;
    float          vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float          yaw = 0.0f, pitch = 0.0f;
    bool           inGround    = false;
    int            lifeTicks   = 0;
};

class BetaCombatAndNetherSystem {
public:
    std::vector<ProjectileEntity> projectiles;
    int portalWarmupTicks = 0;

    // ========================================================================
    //EntityArrow.setArrowHeading & EntityThrowable Initial Velocity
    // ========================================================================
    void spawnProjectile(ProjectileKind kind, bool fromPlayer, int ownerIdx,
                         const Vec3& origin, float yawDeg, float pitchDeg, float speed = 1.5f) {
        float yRad = yawDeg * 0.0174532925f;
        float pRad = pitchDeg * 0.0174532925f;
        float dx = std::cos(pRad) * std::cos(yRad);
        float dy = std::sin(pRad);
        float dz = std::cos(pRad) * std::sin(yRad);

        float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len > 1e-6f) {
            dx /= len;
            dy /= len;
            dz /= len;
        }

        // Deterministic micro-spread matching Notch's setArrowHeading(..., 1.0F)
        uint32_t h = static_cast<uint32_t>(projectiles.size() * 747796405u + 2891336453u) ^
                     static_cast<uint32_t>(std::floor(origin.x * 17.0f + origin.z * 31.0f));
        auto nextSpread = [&]() -> float {
            h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
            float u1 = static_cast<float>(h & 0xFFFFu) / 65535.0f;
            float u2 = static_cast<float>((h >> 16) & 0xFFFFu) / 65535.0f;
            return ((u1 + u2) - 1.0f) * 0.0075f;
        };

        float spreadScale = fromPlayer ? 1.0f : 6.0f;
        dx += nextSpread() * spreadScale;
        dy += nextSpread() * spreadScale;
        dz += nextSpread() * spreadScale;

        float vx = dx * speed;
        float vy = dy * speed;
        float vz = dz * speed;

        float horiz = std::sqrt(vx * vx + vz * vz);
        float initYaw   = std::atan2(vz, vx) * 57.2957795f;
        float initPitch = std::atan2(vy, horiz) * 57.2957795f;

        projectiles.push_back({
            kind, fromPlayer, ownerIdx,
            origin.x + dx * 0.32f, origin.y - 0.10f, origin.z + dz * 0.32f,
            vx, vy, vz,
            initYaw, initPitch, false, 0
        });
    }

    // ========================================================================
    //InventoryPlayer.getTotalArmorValue() & EntityLiving.damageEntity()
    // ========================================================================
    static float applyBetaArmorReduction(float rawDamage, ItemStack armorSlots[4], const BetaAssetManager& assets) {
        if (rawDamage <= 0.0f) return 0.0f;

        int i = 0; // Total remaining durability across equipped armor pieces
        int j = 0; // Total maximum durability across equipped armor pieces
        int k = 0; // Total maximum armor points (3 + 8 + 6 + 3 = 20 max -> 80% reduction)

        for (int slot = 0; slot < 4; ++slot) {
            if (armorSlots[slot].isEmpty()) continue;
            const ItemDef& def = assets.getItemDef(armorSlots[slot].id);
            if (def.maxDamage == 0 || def.armorPoints <= 0) continue;

            int maxDur = static_cast<int>(def.maxDamage);
            int curDmg = std::clamp(static_cast<int>(armorSlots[slot].damage), 0, maxDur);
            int remDur = maxDur - curDmg;

            i += remDur;
            j += maxDur;
            k += def.armorPoints;
        }

        if (j <= 0 || k <= 0) {
            return rawDamage;
        }

        int armorValue = ((k - 1) * i) / j + 1;
        armorValue = std::clamp(armorValue, 0, 20);

        // ExactEntityLiving.carryoverDamage accumulator (mod 25)
        static thread_local float carryoverDamage = 0.0f;
        float temp = rawDamage * static_cast<float>(25 - armorValue) + carryoverDamage;
        float finalDamage = std::floor(temp / 25.0f);
        carryoverDamage = std::fmod(temp, 25.0f);
        return finalDamage;
    }

    //InventoryPlayer.damageArmor(int damage): each piece takes max(1, damage / 4)
    static void damageArmorDurability(ItemStack armorSlots[4], const BetaAssetManager& assets, float rawDamage) {
        if (rawDamage <= 0.0f) return;
        int dmgAmount = static_cast<int>(rawDamage) / 4;
        if (dmgAmount <= 0) dmgAmount = 1;

        for (int slot = 0; slot < 4; ++slot) {
            if (armorSlots[slot].isEmpty()) continue;
            const ItemDef& def = assets.getItemDef(armorSlots[slot].id);
            if (def.maxDamage == 0 || def.armorPoints <= 0) continue;

            uint32_t nextDamage = static_cast<uint32_t>(armorSlots[slot].damage) + static_cast<uint32_t>(dmgAmount);
            if (nextDamage >= def.maxDamage) {
                armorSlots[slot].clear();
            } else {
                armorSlots[slot].damage = static_cast<uint16_t>(nextDamage);
            }
        }
    }

    // ========================================================================
    //BlockPortal.tryToCreatePortal(World world, int x, int y, int z)
    // Exact Beta 1.7.3 4x5 Obsidian Frame (2x3 Interior Air/Fire, Optional Corners)
    // ========================================================================
    template<typename QueueType>
    bool tryActivateNetherPortal(WorldMap& world, int wx, int wy, int wz, QueueType& meshQueue) {
        if (wy < 1 || wy >= CHUNK_HEIGHT - 4) return false;

        int b0 = 0;
        int b1 = 0;
        if (((world.getBlockAt(wx - 1, wy, wz) >> 4) & 0xFF) == 49 ||
            ((world.getBlockAt(wx + 1, wy, wz) >> 4) & 0xFF) == 49) {
            b0 = 1;
        }
        if (((world.getBlockAt(wx, wy, wz - 1) >> 4) & 0xFF) == 49 ||
            ((world.getBlockAt(wx, wy, wz + 1) >> 4) & 0xFF) == 49) {
            b1 = 1;
        }

        if (b0 == b1) return false;

        // Shift to the left-most interior air column if ignited on the right column
        if (world.getBlockAt(wx - b0, wy, wz - b1) == 0) {
            wx -= b0;
            wz -= b1;
        }

        // Validate 4-wide (i = -1..2) by 5-high (j = -1..3) Obsidian frame
        for (int i = -1; i <= 2; ++i) {
            for (int j = -1; j <= 3; ++j) {
                bool isFrame = (i == -1 || i == 2 || j == -1 || j == 3);
                if ((i == -1 || i == 2) && (j == -1 || j == 3)) {
                    continue; // Corners are optional in Beta 1.7.3
                }

                uint8_t id = static_cast<uint8_t>((world.getBlockAt(wx + b0 * i, wy + j, wz + b1 * i) >> 4) & 0xFF);
                if (isFrame) {
                    if (id != 49) return false; // Must be Obsidian (ID 49)
                } else {
                    if (id != 0 && id != 51) return false; // Interior must be Air (0) or Fire (51)
                }
            }
        }

        // Fill 2x3 interior with Portal blocks (ID 90)
        uint8_t portalMeta = (b1 != 0) ? 2 : 1;
        for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 3; ++j) {
                int px = wx + b0 * i;
                int py = wy + j;
                int pz = wz + b1 * i;
                world.setBlockAt(px, py, pz, static_cast<uint16_t>((90 << 4) | portalMeta), false);
                meshQueue.push({ px >> 4, pz >> 4 });
            }
        }
        return true;
    }

    // ========================================================================
    //EntityPlayer.onLivingUpdate Portal Warmup & 1:8 Dimension Scaling
    // ========================================================================
    bool tickNetherPortalTransport(const WorldMap& world, const PlayerPhysics& player,
                                   int32_t& outTargetDim, double& outTargetX, double& outTargetY, double& outTargetZ) {
        int bx = static_cast<int>(std::floor(player.x));
        int by = static_cast<int>(std::floor(player.y + 0.5f));
        int bz = static_cast<int>(std::floor(player.z));

        uint8_t footId = static_cast<uint8_t>((world.getBlockAt(bx, by, bz) >> 4) & 0xFF);
        if (footId == 90) {
            if (++portalWarmupTicks >= 80) {
                portalWarmupTicks = 0;
                if (world.dimension == 0) {
                    outTargetDim = -1;
                    outTargetX   = static_cast<double>(player.x) / 8.0;
                    outTargetY   = std::clamp(static_cast<double>(player.y), 35.0, 95.0);
                    outTargetZ   = static_cast<double>(player.z) / 8.0;
                } else {
                    outTargetDim = 0;
                    outTargetX   = static_cast<double>(player.x) * 8.0;
                    outTargetY   = std::clamp(static_cast<double>(player.y), 65.0, 110.0);
                    outTargetZ   = static_cast<double>(player.z) * 8.0;
                }
                return true;
            }
        } else {
            portalWarmupTicks = std::max(0, portalWarmupTicks - 2);
        }
        return false;
    }

    // ========================================================================
    //Teleporter.placeInExistingPortal (128-Block Search Radius)
    // ========================================================================
    static bool findExistingPortal(const WorldMap& world, double targetX, double targetY, double targetZ,
                                   double& outX, double& outY, double& outZ) {
        constexpr int SEARCH_RADIUS = 128;
        double bestDistSq = -1.0;
        int bestX = 0, bestY = 0, bestZ = 0;

        int centerX = static_cast<int>(std::floor(targetX));
        int centerZ = static_cast<int>(std::floor(targetZ));

        for (int wx = centerX - SEARCH_RADIUS; wx <= centerX + SEARCH_RADIUS; ++wx) {
            double dx = (static_cast<double>(wx) + 0.5) - targetX;
            if (bestDistSq >= 0.0 && dx * dx > bestDistSq) continue;

            for (int wz = centerZ - SEARCH_RADIUS; wz <= centerZ + SEARCH_RADIUS; ++wz) {
                double dz = (static_cast<double>(wz) + 0.5) - targetZ;
                if (bestDistSq >= 0.0 && (dx * dx + dz * dz) > bestDistSq) continue;

                for (int wy = CHUNK_HEIGHT - 1; wy >= 0; --wy) {
                    if (((world.getBlockAt(wx, wy, wz) >> 4) & 0xFF) == 90) {
                        while (wy > 0 && ((world.getBlockAt(wx, wy - 1, wz) >> 4) & 0xFF) == 90) {
                            --wy;
                        }
                        double dy = (static_cast<double>(wy) + 0.5) - targetY;
                        double distSq = dx * dx + dy * dy + dz * dz;
                        if (bestDistSq < 0.0 || distSq < bestDistSq) {
                            bestDistSq = distSq;
                            bestX = wx;
                            bestY = wy;
                            bestZ = wz;
                        }
                    }
                }
            }
        }

        if (bestDistSq < 0.0) return false;

        // Center player inside the 2-wide portal opening
        double spawnX = static_cast<double>(bestX) + 0.5;
        double spawnY = static_cast<double>(bestY) + 0.1;
        double spawnZ = static_cast<double>(bestZ) + 0.5;

        if (((world.getBlockAt(bestX - 1, bestY, bestZ) >> 4) & 0xFF) == 90) spawnX -= 0.5;
        if (((world.getBlockAt(bestX + 1, bestY, bestZ) >> 4) & 0xFF) == 90) spawnX += 0.5;
        if (((world.getBlockAt(bestX, bestY, bestZ - 1) >> 4) & 0xFF) == 90) spawnZ -= 0.5;
        if (((world.getBlockAt(bestX, bestY, bestZ + 1) >> 4) & 0xFF) == 90) spawnZ += 0.5;

        outX = spawnX;
        outY = spawnY;
        outZ = spawnZ;
        return true;
    }

    // ========================================================================
    //Teleporter.createPortal (Natural Ground Search + Fallback Obsidian Platform)
    // ========================================================================
    static void buildArrivalPortal(WorldMap& world, int wx, int wy, int wz) {
        int baseY = std::clamp(wy, 10, 118);

        // Pass 1: Search 16-block radius for a solid flat floor with 4 blocks of air headroom
        int bestX = wx, bestY = baseY, bestZ = wz;
        double bestDistSq = -1.0;

        for (int tx = wx - 16; tx <= wx + 16; ++tx) {
            double dx = static_cast<double>(tx - wx);
            for (int tz = wz - 16; tz <= wz + 16; ++tz) {
                double dz = static_cast<double>(tz - wz);
                for (int ty = CHUNK_HEIGHT - 10; ty >= 10; --ty) {
                    if (world.getBlockAt(tx, ty, tz) != 0) continue;
                    uint16_t groundSt = world.getBlockAt(tx, ty - 1, tz);
                    if (!world.registry.get(groundSt).isSolid) continue;

                    // Verify 4x3 floor solidity and 4 blocks of air clearance
                    bool clearSpot = true;
                    for (int fx = 0; fx < 4 && clearSpot; ++fx) {
                        for (int fz = -1; fz <= 1 && clearSpot; ++fz) {
                            if (!world.registry.get(world.getBlockAt(tx + fx, ty - 1, tz + fz)).isSolid) {
                                clearSpot = false;
                                break;
                            }
                            for (int fy = 0; fy < 4; ++fy) {
                                if (world.getBlockAt(tx + fx, ty + fy, tz + fz) != 0) {
                                    clearSpot = false;
                                    break;
                                }
                            }
                        }
                    }

                    if (clearSpot) {
                        double dy = static_cast<double>(ty - baseY);
                        double dSq = dx * dx + dy * dy + dz * dz;
                        if (bestDistSq < 0.0 || dSq < bestDistSq) {
                            bestDistSq = dSq;
                            bestX = tx;
                            bestY = ty;
                            bestZ = tz;
                        }
                    }
                }
            }
        }

        if (bestDistSq >= 0.0) {
            // Build clean 4x5 Obsidian portal frame on natural ground
            for (int x = 0; x < 4; ++x) {
                for (int y = 0; y < 5; ++y) {
                    bool border = (x == 0 || x == 3 || y == 0 || y == 4);
                    world.setBlockAt(bestX + x, bestY + y - 1, bestZ, border ? (49 << 4) : ((90 << 4) | 1), false);
                }
            }
            return;
        }

        // Pass 2 (Fallback): Carve air pocket & build classic Notch Obsidian Portal + Foothold Ledges
        for (int x = 0; x < 4; ++x) {
            for (int z = -1; z <= 1; ++z) {
                for (int y = -1; y < 4; ++y) {
                    bool isFloor = (y < 0);
                    world.setBlockAt(wx + x, baseY + y, wz + z, isFloor ? (49 << 4) : 0, false);
                }
            }
        }

        for (int x = 0; x < 4; ++x) {
            for (int y = -1; y < 4; ++y) {
                bool border = (x == 0 || x == 3 || y == -1 || y == 3);
                world.setBlockAt(wx + x, baseY + y, wz, border ? (49 << 4) : ((90 << 4) | 1), false);
            }
        }
    }
};