#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "world/world.hpp"

#include <cstdint>
#include <cmath>
#include <algorithm>

// ============================================================================
// Minecraft Beta 1.7.3 Player Physics, View Bobbing & Block Breaking (1:1)
// ============================================================================

struct AABB {
    float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
    float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;

    static AABB fromFootPos(float fx, float fy, float fz, float halfW, float height) {
        return { fx - halfW, fy, fz - halfW, fx + halfW, fy + height, fz + halfW };
    }

    AABB offset(float dx, float dy, float dz) const {
        return { minX + dx, minY + dy, minZ + dz, maxX + dx, maxY + dy, maxZ + dz };
    }

    bool intersects(const AABB& o) const {
        return (maxX > o.minX && minX < o.maxX) &&
               (maxY > o.minY && minY < o.maxY) &&
               (maxZ > o.minZ && minZ < o.maxZ);
    }
};

struct RaycastHit {
    bool  hit      = false;
    int   bx = 0, by = 0, bz = 0;
    int   nx = 0, ny = 0, nz = 0;
    float distance = 0.0f;
};

class PlayerPhysics {
public:
    static constexpr float HALF_WIDTH   = 0.30f;
    static constexpr float STAND_HEIGHT = 1.80f;
    static constexpr float SNEAK_HEIGHT = 1.65f;
    static constexpr float EYE_HEIGHT   = 1.62f;
    static constexpr float STEP_HEIGHT  = 0.50f;

    float x = 0.5f, y = 66.0f, z = 0.5f;
    float prevX = 0.5f, prevY = 66.0f, prevZ = 0.5f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;

    float yaw   = 0.0f;
    float pitch = 0.0f;

    float health       = 20.0f;
    int   airSupply    = 300;
    int   hurtTime     = 0;
    int   fireTicks    = 0;
    float fallDistance = 0.0f;

    bool  onGround     = false;
    bool  sneaking     = false;
    bool  flying       = false;
    bool  inWater      = false;
    bool  eyesInWater  = false;
    bool  inLava       = false;

    float swingProgress = 0.0f;
    bool  isSwinging    = false;
    float walkDist      = 0.0f;
    float prevWalkDist  = 0.0f;
    float cameraYaw     = 0.0f; // Horizontal speed bob amplitude (EntityPlayer.java)
    float prevCameraYaw = 0.0f;

    int   miningX = 0, miningY = -1, miningZ = 0;
    float breakProgress = 0.0f;
    int   blockHitDelay = 0;

    void triggerSwing() {
        if (!isSwinging || swingProgress > 0.5f) {
            isSwinging = true;
            swingProgress = 0.0f;
        }
    }

    void getInterpolatedEyePos(float alpha, float& outX, float& outY, float& outZ) const {
        float eyeOff = sneaking ? (EYE_HEIGHT - 0.08f) : EYE_HEIGHT;
        float ix = prevX + (x - prevX) * alpha;
        float iy = prevY + (y - prevY) * alpha + eyeOff;
        float iz = prevZ + (z - prevZ) * alpha;

        // Mojang EntityRenderer.setupViewBobbing vertical & lateral sway
        float wd  = prevWalkDist + (walkDist - prevWalkDist) * alpha;
        float bob = prevCameraYaw + (cameraYaw - prevCameraYaw) * alpha;
        float yRad = yaw * 0.0174532925f;
        float rightX = -std::sin(yRad);
        float rightZ =  std::cos(yRad);

        float lat = std::sin(wd * 3.14159265f) * bob * 0.5f;
        float ver = -std::abs(std::cos(wd * 3.14159265f) * bob);

        outX = ix + rightX * lat;
        outY = iy + ver;
        outZ = iz + rightZ * lat;
    }

    void heal(float amount) {
        if (health > 0.0f) {
            health = std::min(20.0f, health + amount);
        }
    }

    void damage(float amount, AudioEngine* audio = nullptr, const BetaAssetManager* assets = nullptr) {
        if (flying || hurtTime > 0 || health <= 0.0f) return;
        health = std::max(0.0f, health - amount);
        hurtTime = 10;
        if (audio && assets && !assets->hitSound.samples.empty()) {
            audio->play2D(assets->hitSound, 1.0f, 1.0f);
        }
    }

    void tick(const WorldMap& world, float forwardInput, float strafeInput, bool jumpInput,
              AudioEngine* audio = nullptr, const BetaAssetManager* assets = nullptr) {
        prevX = x; prevY = y; prevZ = z;
        prevWalkDist  = walkDist;
        prevCameraYaw = cameraYaw;

        if (hurtTime > 0) hurtTime--;
        if (blockHitDelay > 0) blockHitDelay--;

        if (isSwinging) {
            swingProgress += 0.1666f;
            if (swingProgress >= 1.0f) {
                swingProgress = 0.0f;
                isSwinging = false;
            }
        }

        int bx = (int)std::floor(x), by = (int)std::floor(y + 0.1f), bz = (int)std::floor(z);
        int eyeBy = (int)std::floor(y + EYE_HEIGHT);
        uint8_t footId = (uint8_t)((world.getBlockAt(bx, by, bz) >> 4) & 0xFF);
        uint8_t eyeId  = (uint8_t)((world.getBlockAt(bx, eyeBy, bz) >> 4) & 0xFF);

        inWater     = (footId == 8 || footId == 9);
        eyesInWater = (eyeId == 8 || eyeId == 9);
        inLava      = (footId == 10 || footId == 11);
        bool onLadder = (footId == 65);

        if (eyesInWater && !flying) {
            if (--airSupply <= -20) {
                airSupply = 0;
                damage(2.0f, audio, assets);
            }
        } else {
            airSupply = 300;
        }

        if (inLava && !flying) {
            fireTicks = 300;
            if ((world.worldTick % 10) == 0) damage(4.0f, audio, assets);
        }
        if (fireTicks > 0) {
            fireTicks--;
            if (inWater) fireTicks = 0;
            else if ((fireTicks % 20) == 0) damage(1.0f, audio, assets);
        }

        float yawRad = yaw * 0.0174532925f;
        float fwdX = std::cos(yawRad), fwdZ = std::sin(yawRad);
        float rightX = -std::sin(yawRad), rightZ = std::cos(yawRad);

        float moveX = fwdX * forwardInput + rightX * strafeInput;
        float moveZ = fwdZ * forwardInput + rightZ * strafeInput;
        float moveLen = std::sqrt(moveX * moveX + moveZ * moveZ);
        if (moveLen > 1.0f) { moveX /= moveLen; moveZ /= moveLen; }

        if (flying) {
            float flySpeed = 0.22f;
            vx = vx * 0.6f + moveX * flySpeed;
            vz = vz * 0.6f + moveZ * flySpeed;
            float vert = (jumpInput ? 1.0f : 0.0f) - (sneaking ? 1.0f : 0.0f);
            vy = vy * 0.6f + vert * 0.18f;
            moveWithCollision(world, vx, vy, vz, false);
            fallDistance = 0.0f;
            cameraYaw += (0.0f - cameraYaw) * 0.4f;
            return;
        }

        if (inWater) {
            float accel = 0.02f;
            vx += moveX * accel;
            vz += moveZ * accel;
            if (jumpInput) vy += 0.04f;
            vy -= 0.02f;
            moveWithCollision(world, vx, vy, vz, false);
            vx *= 0.80f; vy *= 0.80f; vz *= 0.80f;
            fallDistance = 0.0f;
        } else if (inLava) {
            vx += moveX * 0.02f;
            vz += moveZ * 0.02f;
            if (jumpInput) vy += 0.04f;
            vy -= 0.02f;
            moveWithCollision(world, vx, vy, vz, false);
            vx *= 0.50f; vy *= 0.50f; vz *= 0.50f;
            fallDistance = 0.0f;
        } else {
            float slipperiness = 0.91f;
            if (onGround) {
                uint16_t underSt = world.getBlockAt((int)std::floor(x), (int)std::floor(y - 0.2f), (int)std::floor(z));
                slipperiness = world.registry.get(underSt).slipperiness * 0.91f;
            }
            float sneakScale = sneaking ? 0.30f : 1.0f;
            float accel = onGround ? (0.10f * sneakScale * (0.16277136f / (slipperiness * slipperiness * slipperiness)))
                                   : (0.02f * sneakScale);

            vx += moveX * accel;
            vz += moveZ * accel;

            if (onLadder) {
                vx = std::clamp(vx, -0.15f, 0.15f);
                vz = std::clamp(vz, -0.15f, 0.15f);
                vy = std::max(vy, -0.15f);
                if (sneaking && vy < 0.0f) vy = 0.0f;
                if (jumpInput || moveLen > 0.05f) vy = 0.20f;
                fallDistance = 0.0f;
            } else if (jumpInput && onGround) {
                vy = 0.42f;
                onGround = false;
            }

            if (footId == 30 || eyeId == 30) {
                vx *= 0.25f; vy *= 0.05f; vz *= 0.25f;
                fallDistance = 0.0f;
            }

            float prevYBeforeMove = y;
            moveWithCollision(world, vx, vy, vz, sneaking && onGround);

            if (onGround) {
                if (fallDistance > 3.0f) {
                    float dmg = std::floor(fallDistance - 3.0f);
                    if (dmg > 0.0f) damage(dmg, audio, assets);
                }
                fallDistance = 0.0f;
            } else if (y < prevYBeforeMove && !onLadder) {
                fallDistance += (prevYBeforeMove - y);
            }

            vy -= 0.08f;
            vy *= 0.98f;
            vx *= slipperiness;
            vz *= slipperiness;
        }

        // Exact Mojang EntityPlayer.onLivingUpdate view bobbing calculation
        float horizMoved = std::sqrt((x - prevX) * (x - prevX) + (z - prevZ) * (z - prevZ));
        float targetBob  = onGround ? std::min(0.1f, horizMoved) : 0.0f;
        cameraYaw += (targetBob - cameraYaw) * 0.4f;
        if (onGround) walkDist += horizMoved * 0.6f;
    }

    static RaycastHit raycast(const WorldMap& world, float ox, float oy, float oz,
                              float dx, float dy, float dz, float maxDist) {
        RaycastHit out{};
        int bx = (int)std::floor(ox), by = (int)std::floor(oy), bz = (int)std::floor(oz);
        int stepX = (dx > 0.0f) ? 1 : ((dx < 0.0f) ? -1 : 0);
        int stepY = (dy > 0.0f) ? 1 : ((dy < 0.0f) ? -1 : 0);
        int stepZ = (dz > 0.0f) ? 1 : ((dz < 0.0f) ? -1 : 0);

        float tDeltaX = (stepX != 0) ? std::abs(1.0f / dx) : 1e30f;
        float tDeltaY = (stepY != 0) ? std::abs(1.0f / dy) : 1e30f;
        float tDeltaZ = (stepZ != 0) ? std::abs(1.0f / dz) : 1e30f;

        float tMaxX = (stepX > 0) ? ((bx + 1.0f - ox) * tDeltaX) : ((ox - bx) * tDeltaX);
        float tMaxY = (stepY > 0) ? ((by + 1.0f - oy) * tDeltaY) : ((oy - by) * tDeltaY);
        float tMaxZ = (stepZ > 0) ? ((bz + 1.0f - oz) * tDeltaZ) : ((oz - bz) * tDeltaZ);

        int nx = 0, ny = 0, nz = 0;
        float dist = 0.0f;

        for (int i = 0; i < 64 && dist <= maxDist; ++i) {
            uint16_t st = world.getBlockAt(bx, by, bz);
            const BlockDef& def = world.registry.get(st);
            if (def.shape != SHAPE_NONE && def.shape != SHAPE_FLUID) {
                out.hit = true;
                out.bx = bx; out.by = by; out.bz = bz;
                out.nx = nx; out.ny = ny; out.nz = nz;
                out.distance = dist;
                return out;
            }

            if (tMaxX < tMaxY && tMaxX < tMaxZ) {
                dist = tMaxX; tMaxX += tDeltaX; bx += stepX;
                nx = -stepX; ny = 0; nz = 0;
            } else if (tMaxY < tMaxZ) {
                dist = tMaxY; tMaxY += tDeltaY; by += stepY;
                nx = 0; ny = -stepY; nz = 0;
            } else {
                dist = tMaxZ; tMaxZ += tDeltaZ; bz += stepZ;
                nx = 0; ny = 0; nz = -stepZ;
            }
        }
        return out;
    }

    float computeBlockDamagePerTick(const WorldMap& world, const BetaAssetManager& assets,
                                    int bx, int by, int bz, uint16_t heldId) const {
        uint16_t st = world.getBlockAt(bx, by, bz);
        const BlockDef& def = world.registry.get(st);
        if (def.hardness < 0.0f) return 0.0f;
        if (def.hardness == 0.0f) return 1.0f;

        const ItemDef& tool = assets.getItemDef(heldId);
        bool canHarvest = (def.harvestLevel == 0) ||
                          (tool.toolType == def.bestTool && tool.harvestLevel >= def.harvestLevel);

        float speed = 1.0f;
        if (tool.toolType == def.bestTool && tool.toolType != TOOL_NONE) {
            speed = tool.miningSpeed;
        }
        if (eyesInWater) speed *= 0.2f;
        if (!onGround)   speed *= 0.2f;

        float divisor = canHarvest ? 30.0f : 100.0f;
        return (speed / def.hardness) / divisor;
    }

    static void resolveBlockDrops(const WorldMap& world, const BetaAssetManager& assets,
                                  uint16_t brokenState, uint16_t heldId, uint32_t rngSeed,
                                  uint16_t& outId, uint16_t& outMeta, uint8_t& outCount) {
        outId = 0; outMeta = 0; outCount = 0;
        uint8_t id   = (uint8_t)((brokenState >> 4) & 0xFF);
        uint8_t meta = (uint8_t)(brokenState & 0x0F);
        const BlockDef& def = world.registry.getById(id);
        const ItemDef& tool = assets.getItemDef(heldId);

        if (id == 7 || id == 8 || id == 9 || id == 10 || id == 11 || id == 20 || id == 51 || id == 52 || id == 79 || id == 90 || id == 92) {
            return;
        }

        if (def.harvestLevel > 0 && !(tool.toolType == def.bestTool && tool.harvestLevel >= def.harvestLevel)) {
            return;
        }

        switch (id) {
            case 1:  outId = 4; outCount = 1; break;
            case 2:  outId = 3; outCount = 1; break;
            case 13: outId = ((rngSeed % 10) == 0) ? ITEM_FLINT : 13; outCount = 1; break;
            case 16: outId = ITEM_COAL; outCount = 1; break;
            case 56: outId = ITEM_DIAMOND; outCount = 1; break;
            case 73:
            case 74: outId = ITEM_REDSTONE; outCount = (uint8_t)(4 + (rngSeed % 2)); break;
            case 21: outId = ITEM_DYE; outMeta = 4; outCount = (uint8_t)(4 + (rngSeed % 5)); break;
            case 18:
                if (heldId == ITEM_SHEARS) { outId = 18; outMeta = meta & 3; outCount = 1; }
                else if ((rngSeed % 20) == 0) { outId = 6; outMeta = meta & 3; outCount = 1; }
                break;
            case 31:
                if (heldId == ITEM_SHEARS) { outId = 31; outMeta = meta; outCount = 1; }
                else if ((rngSeed % 8) == 0) { outId = ITEM_SEEDS; outCount = 1; }
                break;
            case 47: outId = ITEM_BOOK; outCount = 3; break;
            case 82: outId = ITEM_CLAY_BALL; outCount = 4; break;
            case 89: outId = ITEM_GLOWSTONE_DUST; outCount = (uint8_t)(2 + (rngSeed % 3)); break;
            case 78: outId = ITEM_SNOWBALL; outCount = 1; break;
            case 80: outId = ITEM_SNOWBALL; outCount = 4; break;
            case 64: outId = ITEM_WOOD_DOOR; outCount = 1; break;
            case 71: outId = ITEM_IRON_DOOR; outCount = 1; break;
            case 83: outId = ITEM_REEDS; outCount = 1; break;
            case 55: outId = ITEM_REDSTONE; outCount = 1; break;
            case 93:
            case 94: outId = ITEM_REPEATER_ITEM; outCount = 1; break;
            case 26: outId = ITEM_BED_ITEM; outCount = 1; break;
            case 59:
                if ((meta & 7) >= 7) { outId = ITEM_WHEAT; outCount = 1; }
                else { outId = ITEM_SEEDS; outCount = 1; }
                break;
            default:
                outId = id;
                outMeta = (id == 35 || id == 17 || id == 6 || id == 44) ? meta : 0;
                outCount = 1;
                break;
        }
    }

private:
    bool collidesWithWorld(const WorldMap& world, const AABB& box) const {
        int x0 = (int)std::floor(box.minX), x1 = (int)std::floor(box.maxX);
        int y0 = (int)std::floor(box.minY), y1 = (int)std::floor(box.maxY);
        int z0 = (int)std::floor(box.minZ), z1 = (int)std::floor(box.maxZ);

        SubBoxList subs;
        for (int wy = y0; wy <= y1; ++wy) {
            if (wy < 0 || wy >= CHUNK_HEIGHT) continue;
            for (int wz = z0; wz <= z1; ++wz) {
                for (int wx = x0; wx <= x1; ++wx) {
                    uint16_t st = world.getBlockAt(wx, wy, wz);
                    const BlockDef& def = world.registry.get(st);
                    if (!def.isSolid) continue;

                    if (def.shape == SHAPE_CUBE) {
                        AABB blk{ (float)wx, (float)wy, (float)wz, wx + 1.0f, wy + 1.0f, wz + 1.0f };
                        if (box.intersects(blk)) return true;
                    } else {
                        world.registry.getSubBoxes(st, wx, wy, wz, [&](int qx, int qy, int qz) {
                            return world.getBlockAt(qx, qy, qz);
                        }, subs);
                        for (int b = 0; b < subs.count; ++b) {
                            const SubBox& sb = subs.boxes[b];
                            AABB blk{ wx + sb.minX * 0.0625f, wy + sb.minY * 0.0625f, wz + sb.minZ * 0.0625f,
                                      wx + sb.maxX * 0.0625f, wy + sb.maxY * 0.0625f, wz + sb.maxZ * 0.0625f };
                            if (box.intersects(blk)) return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    void moveWithCollision(const WorldMap& world, float dx, float dy, float dz, bool edgeHang) {
        float h = sneaking ? SNEAK_HEIGHT : STAND_HEIGHT;

        if (edgeHang) {
            const float step = 0.05f;
            while (dx != 0.0f && !collidesWithWorld(world, AABB::fromFootPos(x + dx, y - 0.6f, z, HALF_WIDTH, 0.5f))) {
                if (std::abs(dx) <= step) { dx = 0.0f; break; }
                dx += (dx > 0.0f) ? -step : step;
            }
            while (dz != 0.0f && !collidesWithWorld(world, AABB::fromFootPos(x, y - 0.6f, z + dz, HALF_WIDTH, 0.5f))) {
                if (std::abs(dz) <= step) { dz = 0.0f; break; }
                dz += (dz > 0.0f) ? -step : step;
            }
        }

        AABB boxY = AABB::fromFootPos(x, y + dy, z, HALF_WIDTH, h);
        if (!collidesWithWorld(world, boxY)) {
            y += dy;
            onGround = false;
        } else {
            if (dy < 0.0f) {
                onGround = true;
                y = std::floor(y + dy + 1.0f);
                if (collidesWithWorld(world, AABB::fromFootPos(x, y, z, HALF_WIDTH, h))) y -= 1.0f;
            }
            vy = 0.0f;
        }

        if (!collidesWithWorld(world, AABB::fromFootPos(x + dx, y, z, HALF_WIDTH, h))) {
            x += dx;
        } else if (onGround && !collidesWithWorld(world, AABB::fromFootPos(x + dx, y + STEP_HEIGHT, z, HALF_WIDTH, h))) {
            x += dx;
            y += STEP_HEIGHT;
        } else {
            vx = 0.0f;
        }

        if (!collidesWithWorld(world, AABB::fromFootPos(x, y, z + dz, HALF_WIDTH, h))) {
            z += dz;
        } else if (onGround && !collidesWithWorld(world, AABB::fromFootPos(x, y + STEP_HEIGHT, z + dz, HALF_WIDTH, h))) {
            z += dz;
            y += STEP_HEIGHT;
        } else {
            vz = 0.0f;
        }
    }
};