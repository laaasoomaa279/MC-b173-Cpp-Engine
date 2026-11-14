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
// Minecraft Beta 1.7.3 Classic Combat, Machine-Gun Bow & Nether Portals (1:8)
// ============================================================================
// Authentic Beta 1.7.3 Mechanics:
//   - No Attack Cooldown & No Bow Charging: Clicking fires arrows immediately!
//   - Classic Armor Damage Reduction: Full armor reduces up to 80% damage
//     scaled by remaining armor durability (not fixed material tiers!).
//   - Nether Portal Frame Validation (4x5 up to 23x23, optional corners)
//   - 1:8 Overworld <-> Nether Coordinate Scaling & Obsidian Platform Arrival
// ============================================================================

enum ProjectileKind : uint8_t {
    PROJ_ARROW    = 0,
    PROJ_SNOWBALL = 1,
    PROJ_EGG      = 2
};

struct ProjectileEntity {
    ProjectileKind kind         = PROJ_ARROW;
    bool           fromPlayer   = true;
    int            ownerMobIdx  = -1;
    float          x = 0.0f, y = 0.0f, z = 0.0f;
    float          vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float          yaw = 0.0f, pitch = 0.0f;
    bool           inGround     = false;
    int            lifeTicks    = 0;
};

class BetaCombatAndNetherSystem {
public:
    std::vector<ProjectileEntity> projectiles;
    int portalWarmupTicks = 0;

    void spawnProjectile(ProjectileKind kind, bool fromPlayer, int ownerIdx,
                         const Vec3& origin, float yawDeg, float pitchDeg, float speed = 1.5f) {
        float yRad = yawDeg * 0.0174532925f;
        float pRad = pitchDeg * 0.0174532925f;
        float dx = std::cos(pRad) * std::cos(yRad);
        float dy = std::sin(pRad);
        float dz = std::cos(pRad) * std::sin(yRad);

        projectiles.push_back({
            kind, fromPlayer, ownerIdx,
            origin.x + dx * 0.4f, origin.y - 0.1f, origin.z + dz * 0.4f,
            dx * speed, dy * speed, dz * speed,
            yawDeg, pitchDeg, false, 0
        });
    }

    // Classic Beta 1.7.3 Armor Reduction Formula (scales with current durability / maxDurability)
    static float applyBetaArmorReduction(float rawDamage, ItemStack armorSlots[4], const BetaAssetManager& assets) {
        int currentPoints = 0;
        int maxPoints     = 0;
        for (int i = 0; i < 4; ++i) {
            if (armorSlots[i].isEmpty()) continue;
            const ItemDef& def = assets.getItemDef(armorSlots[i].id);
            if (def.maxDamage == 0 || def.armorPoints == 0) continue;
            int remDur = std::max(0, (int)def.maxDamage - (int)armorSlots[i].damage);
            currentPoints += (def.armorPoints * remDur) / (int)def.maxDamage;
            maxPoints     += def.armorPoints;
        }
        if (maxPoints <= 0) return rawDamage;
        float absorbRatio = 0.80f * ((float)currentPoints / 25.0f);
        absorbRatio = std::clamp(absorbRatio, 0.0f, 0.80f);
        return rawDamage * (1.0f - absorbRatio);
    }

    // Nether Portal Activation with Flint & Steel (Block ID 49 Obsidian -> ID 90 Portal)
    template<typename QueueType>
    bool tryActivateNetherPortal(WorldMap& world, int wx, int wy, int wz, QueueType& meshQueue) {
        // Test X-aligned and Z-aligned portal frames
        for (int axis = 0; axis < 2; ++axis) {
            int dx = (axis == 0) ? 1 : 0;
            int dz = (axis == 1) ? 1 : 0;

            int minOff = 0;
            while (minOff > -21 && world.getBlockAt(wx + (minOff - 1) * dx, wy, wz + (minOff - 1) * dz) == 0) minOff--;
            int width = 0;
            while (width < 21 && world.getBlockAt(wx + (minOff + width) * dx, wy, wz + (minOff + width) * dz) == 0) width++;
            if (width < 2 || width > 21) continue;

            int minY = wy;
            while (minY > 1 && world.getBlockAt(wx + minOff * dx, minY - 1, wz + minOff * dz) == 0) minY--;
            int height = 0;
            while (height < 21 && world.getBlockAt(wx + minOff * dx, minY + height, wz + minOff * dz) == 0) height++;
            if (height < 3 || height > 21) continue;

            bool validFrame = true;
            for (int w = -1; w <= width && validFrame; ++w) {
                for (int h = -1; h <= height && validFrame; ++h) {
                    bool isCorner = (w == -1 || w == width) && (h == -1 || h == height);
                    if (isCorner) continue;
                    bool isBorder = (w == -1 || w == width || h == -1 || h == height);
                    uint8_t id = (uint8_t)((world.getBlockAt(wx + (minOff + w) * dx, minY + h, wz + (minOff + w) * dz) >> 4) & 0xFF);
                    if (isBorder && id != 49) validFrame = false;
                    if (!isBorder && id != 0 && id != 51) validFrame = false;
                }
            }

            if (validFrame) {
                for (int w = 0; w < width; ++w) {
                    for (int h = 0; h < height; ++h) {
                        int px = wx + (minOff + w) * dx;
                        int pz = wz + (minOff + w) * dz;
                        world.setBlockAt(px, minY + h, pz, (90 << 4), false);
                        meshQueue.push({ px >> 4, pz >> 4 });
                    }
                }
                return true;
            }
        }
        return false;
    }

    // Checks if player stands inside Portal Block (ID 90) for 80 ticks (4 seconds)
    bool tickNetherPortalTransport(const WorldMap& world, const PlayerPhysics& player,
                                   int32_t& outTargetDim, double& outTargetX, double& outTargetY, double& outTargetZ) {
        int bx = (int)std::floor(player.x);
        int by = (int)std::floor(player.y + 0.5f);
        int bz = (int)std::floor(player.z);

        uint8_t footId = (uint8_t)((world.getBlockAt(bx, by, bz) >> 4) & 0xFF);
        if (footId == 90) {
            if (++portalWarmupTicks >= 80) {
                portalWarmupTicks = 0;
                if (world.dimension == 0) {
                    outTargetDim = -1;
                    outTargetX   = player.x / 8.0;
                    outTargetY   = std::clamp((double)player.y, 35.0, 95.0);
                    outTargetZ   = player.z / 8.0;
                } else {
                    outTargetDim = 0;
                    outTargetX   = player.x * 8.0;
                    outTargetY   = std::clamp((double)player.y, 65.0, 110.0);
                    outTargetZ   = player.z * 8.0;
                }
                return true;
            }
        } else {
            portalWarmupTicks = std::max(0, portalWarmupTicks - 2);
        }
        return false;
    }

    // Builds an arrival Obsidian Portal frame in destination dimension
    static void buildArrivalPortal(WorldMap& world, int wx, int wy, int wz) {
        int baseY = std::clamp(wy, 10, 110);
        for (int dx = -1; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dy = 0; dy <= 4; ++dy) {
                    world.setBlockAt(wx + dx, baseY + dy, wz + dz, (dy == 0) ? (49 << 4) : 0, false);
                }
            }
        }
        for (int x = 0; x <= 3; ++x) {
            for (int y = 0; y <= 4; ++y) {
                bool border = (x == 0 || x == 3 || y == 0 || y == 4);
                world.setBlockAt(wx + x, baseY + y, wz, border ? (49 << 4) : (90 << 4), false);
            }
        }
    }
};