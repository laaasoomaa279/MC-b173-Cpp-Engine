#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "world/world.hpp"

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================================
// Minecraft Beta 1.7.3 Player Physics, Collision & Raytracing (1:1 Mojang)
// ============================================================================
// Exact Reproduction of Notch's Beta 1.7.3 Source Code:
//   - AxisAlignedBB.java:
//       * Exact calculateXOffset, calculateYOffset, calculateZOffset &
//         calculateIntercept (Ray-AABB slab test with face normal resolution)
//   - Entity.java & EntityLiving.java & EntityPlayer.java:
//       * Exact 3-axis AABB sweep (Y -> X -> Z) + stepHeight = 0.5F
//       * Sneaking edge-hang protection (0.05D step-back on X, Z, & diagonal XZ)
//       * Exact Fence 1.5-block collision height (BlockFence) & Soul Sand
//         0.875-block height + 0.4x velocity dampening (BlockSoulSand)
//       * Exact Water current flow push, shore jump-out boost (vy = 0.3F),
//         Lava fire ignition (300 ticks), Cobweb 0.25x/0.05x drag, Cactus
//         contact damage, Fire ignition, Suffocation & Void damage
//       * Exact Slipperiness formula: 0.16277136F / (slipperiness^3)
//       * Exact View Bobbing (EntityRenderer.setupViewBobbing) & Footstep
//         sound cadence (distanceWalkedModified / nextStepDistance)
//   - World.rayTraceBlocks & Block.collisionRayTrace:
//       * Sub-voxel 1/16 ray-AABB intersection for Stairs, Slabs, Fences,
//         Doors, Trapdoors, Torches, Repeaters, Buttons, Levers, Cake & Beds
//   - Block.blockStrength & canHarvestBlock:
//       * Exact 1/30 harvestable vs. 1/100 non-harvestable break speed,
//         water & airborne 0.2x penalties, and complete Beta 1.7.3 loot drops
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

    AABB expand(float ex, float ey, float ez) const {
        return { minX - ex, minY - ey, minZ - ez, maxX + ex, maxY + ey, maxZ + ez };
    }

    AABB addCoord(float dx, float dy, float dz) const {
        AABB r = *this;
        if (dx < 0.0f) r.minX += dx;
        if (dx > 0.0f) r.maxX += dx;
        if (dy < 0.0f) r.minY += dy;
        if (dy > 0.0f) r.maxY += dy;
        if (dz < 0.0f) r.minZ += dz;
        if (dz > 0.0f) r.maxZ += dz;
        return r;
    }

    bool intersects(const AABB& o) const {
        return (maxX > o.minX && minX < o.maxX) &&
               (maxY > o.minY && minY < o.maxY) &&
               (maxZ > o.minZ && minZ < o.maxZ);
    }

    //AxisAlignedBB.calculateXOffset(AxisAlignedBB other, double dx)
    float calculateXOffset(const AABB& other, float dx) const {
        if (other.maxY <= minY || other.minY >= maxY) return dx;
        if (other.maxZ <= minZ || other.minZ >= maxZ) return dx;
        if (dx > 0.0f && other.maxX <= minX) {
            float d = minX - other.maxX;
            if (d < dx) dx = d;
        }
        if (dx < 0.0f && other.minX >= maxX) {
            float d = maxX - other.minX;
            if (d > dx) dx = d;
        }
        return dx;
    }

    //AxisAlignedBB.calculateYOffset(AxisAlignedBB other, double dy)
    float calculateYOffset(const AABB& other, float dy) const {
        if (other.maxX <= minX || other.minX >= maxX) return dy;
        if (other.maxZ <= minZ || other.minZ >= maxZ) return dy;
        if (dy > 0.0f && other.maxY <= minY) {
            float d = minY - other.maxY;
            if (d < dy) dy = d;
        }
        if (dy < 0.0f && other.minY >= maxY) {
            float d = maxY - other.minY;
            if (d > dy) dy = d;
        }
        return dy;
    }

    //AxisAlignedBB.calculateZOffset(AxisAlignedBB other, double dz)
    float calculateZOffset(const AABB& other, float dz) const {
        if (other.maxX <= minX || other.minX >= maxX) return dz;
        if (other.maxY <= minY || other.minY >= maxY) return dz;
        if (dz > 0.0f && other.maxZ <= minZ) {
            float d = minZ - other.maxZ;
            if (d < dz) dz = d;
        }
        if (dz < 0.0f && other.minZ >= maxZ) {
            float d = maxZ - other.minZ;
            if (d > dz) dz = d;
        }
        return dz;
    }

    //AxisAlignedBB.calculateIntercept(Vec3D start, Vec3D end)
    bool calculateIntercept(float ox, float oy, float oz,
                            float dx, float dy, float dz, float maxDist,
                            float& outDist, int& outNx, int& outNy, int& outNz) const {
        float tMin = 0.0f;
        float tMax = maxDist;
        int nx = 0, ny = 0, nz = 0;

        // X slab
        if (std::abs(dx) < 1e-7f) {
            if (ox < minX || ox > maxX) return false;
        } else {
            float invD = 1.0f / dx;
            float t1 = (minX - ox) * invD;
            float t2 = (maxX - ox) * invD;
            int hitNx = -1;
            if (t1 > t2) { std::swap(t1, t2); hitNx = 1; }
            if (t1 > tMin) { tMin = t1; nx = hitNx; ny = 0; nz = 0; }
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax) return false;
        }

        // Y slab
        if (std::abs(dy) < 1e-7f) {
            if (oy < minY || oy > maxY) return false;
        } else {
            float invD = 1.0f / dy;
            float t1 = (minY - oy) * invD;
            float t2 = (maxY - oy) * invD;
            int hitNy = -1;
            if (t1 > t2) { std::swap(t1, t2); hitNy = 1; }
            if (t1 > tMin) { tMin = t1; nx = 0; ny = hitNy; nz = 0; }
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax) return false;
        }

        // Z slab
        if (std::abs(dz) < 1e-7f) {
            if (oz < minZ || oz > maxZ) return false;
        } else {
            float invD = 1.0f / dz;
            float t1 = (minZ - oz) * invD;
            float t2 = (maxZ - oz) * invD;
            int hitNz = -1;
            if (t1 > t2) { std::swap(t1, t2); hitNz = 1; }
            if (t1 > tMin) { tMin = t1; nx = 0; ny = 0; nz = hitNz; }
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax) return false;
        }

        outDist = tMin;
        outNx = nx; outNy = ny; outNz = nz;
        return true;
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

    //Entity.moveEntity state flags & footstep cadence
    bool  isCollidedHorizontally = false;
    bool  isCollidedVertically   = false;
    float distanceWalkedModified = 0.0f;
    float nextStepDistance       = 1.0f;
    int   ridingEntityIdx        = -1;

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

        //EntityRenderer.setupViewBobbing vertical & lateral sway
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
            swingProgress += 0.16666667f;
            if (swingProgress >= 1.0f) {
                swingProgress = 0.0f;
                isSwinging = false;
            }
        }

        // Void damage (Entity.onEntityUpdate: y < -64.0)
        if (y < -64.0f) {
            health = std::max(0.0f, health - 4.0f);
        }

        float h = sneaking ? SNEAK_HEIGHT : STAND_HEIGHT;
        AABB plrBox = AABB::fromFootPos(x, y, z, HALF_WIDTH, h);

        int bx = static_cast<int>(std::floor(x));
        int by = static_cast<int>(std::floor(y + 0.1f));
        int bz = static_cast<int>(std::floor(z));
        int eyeBy = static_cast<int>(std::floor(y + (sneaking ? (EYE_HEIGHT - 0.08f) : EYE_HEIGHT)));

        uint16_t footSt = world.getBlockAt(bx, by, bz);
        uint16_t eyeSt  = world.getBlockAt(bx, eyeBy, bz);
        uint8_t footId  = static_cast<uint8_t>((footSt >> 4) & 0xFF);
        uint8_t eyeId   = static_cast<uint8_t>((eyeSt >> 4) & 0xFF);

        //World.handleMaterialAcceleration (Water AABB check + current push)
        Vec3 waterFlow{ 0.0f, 0.0f, 0.0f };
        inWater = checkWaterAndFlow(world, plrBox.expand(-0.001f, -0.401f, -0.001f), waterFlow);
        eyesInWater = (eyeId == 8 || eyeId == 9);
        inLava = checkMaterialInAABB(world, plrBox.expand(-0.1f, -0.4f, -0.1f), 10, 11);
        bool onLadder = (footId == 65 || eyeId == 65);

        // Suffocation check inside solid opaque block (EntityPlayer.isEntityInsideOpaqueBlock)
        if (!flying && hurtTime == 0 && (world.worldTick % 10) == 0) {
            const BlockDef& eyeDef = world.registry.get(eyeSt);
            if (eyeDef.isOpaque && eyeDef.isSolid && eyeDef.shape == SHAPE_CUBE) {
                damage(1.0f, audio, assets);
            }
        }

        // Underwater air supply (300 ticks = 15s, 2 damage per second at -20)
        if (eyesInWater && !flying) {
            if (--airSupply <= -20) {
                airSupply = 0;
                damage(2.0f, audio, assets);
            }
        } else {
            airSupply = 300;
        }

        // Lava & Fire burning (Entity.setOnFireFromLava & BlockFire)
        if (inLava && !flying) {
            fireTicks = 300;
            if ((world.worldTick % 10) == 0) damage(4.0f, audio, assets);
        }
        if (footId == 51 || eyeId == 51) {
            if (fireTicks <= 0 && !flying) damage(1.0f, audio, assets);
            fireTicks = std::max(fireTicks, 160);
        }
        if (fireTicks > 0) {
            fireTicks--;
            if (inWater) fireTicks = 0;
            else if ((fireTicks % 20) == 0) damage(1.0f, audio, assets);
        }

        // Cactus contact damage (BlockCactus.onEntityCollidedWithBlock)
        if (!flying && hurtTime == 0 && checkMaterialInAABB(world, plrBox.expand(0.01f, 0.0f, 0.01f), 81, 81)) {
            damage(1.0f, audio, assets);
        }

        // Flying mode (Creative-style flight)
        if (flying) {
            float flySpeed = 0.22f;
            applyMoveFlying(strafeInput, forwardInput, flySpeed);
            vx *= 0.6f;
            vz *= 0.6f;
            float vert = (jumpInput ? 1.0f : 0.0f) - (sneaking ? 1.0f : 0.0f);
            vy = vy * 0.6f + vert * 0.18f;
            moveWithCollision(world, vx, vy, vz, false, audio, assets);
            fallDistance = 0.0f;
            cameraYaw += (0.0f - cameraYaw) * 0.4f;
            return;
        }

        // Cobweb slowdown (Entity.setInWeb: 0.25x horizontal, 0.05x vertical)
        bool inWeb = checkMaterialInAABB(world, plrBox, 30, 30);
        if (inWeb) {
            vx *= 0.25f;
            vy *= 0.05f;
            vz *= 0.25f;
            fallDistance = 0.0f;
        }

        // Soul Sand slowdown (BlockSoulSand.onEntityCollidedWithBlock: 0.4x horizontal)
        uint16_t underSt = world.getBlockAt(bx, static_cast<int>(std::floor(y - 0.15f)), bz);
        if (((underSt >> 4) & 0xFF) == 88) {
            vx *= 0.4f;
            vz *= 0.4f;
        }

        //EntityLiving.moveEntityWithHeading (Water, Lava, Ground/Air)
        if (inWater) {
            vx += waterFlow.x * 0.014f;
            vy += waterFlow.y * 0.014f;
            vz += waterFlow.z * 0.014f;

            float prevYWater = y;
            applyMoveFlying(strafeInput, forwardInput, 0.02f);
            if (jumpInput) vy += 0.04f;
            moveWithCollision(world, vx, vy, vz, false, audio, assets);
            vx *= 0.80f;
            vy *= 0.80f;
            vz *= 0.80f;
            vy -= 0.02f;

            //shore step-out boost when swimming against a 1-block ledge
            if (isCollidedHorizontally && !collidesWithWorld(world, AABB::fromFootPos(x, y, z, HALF_WIDTH, h).offset(vx, vy + 0.6f - y + prevYWater, vz))) {
                vy = 0.30f;
            }
            fallDistance = 0.0f;
        } else if (inLava) {
            float prevYLava = y;
            applyMoveFlying(strafeInput, forwardInput, 0.02f);
            if (jumpInput) vy += 0.04f;
            moveWithCollision(world, vx, vy, vz, false, audio, assets);
            vx *= 0.50f;
            vy *= 0.50f;
            vz *= 0.50f;
            vy -= 0.02f;

            if (isCollidedHorizontally && !collidesWithWorld(world, AABB::fromFootPos(x, y, z, HALF_WIDTH, h).offset(vx, vy + 0.6f - y + prevYLava, vz))) {
                vy = 0.30f;
            }
            fallDistance = 0.0f;
        } else {
            float slipperiness = 0.91f;
            if (onGround) {
                uint16_t groundSt = world.getBlockAt(
                    static_cast<int>(std::floor(x)),
                    static_cast<int>(std::floor(y - 0.20f)),
                    static_cast<int>(std::floor(z))
                );
                slipperiness = world.registry.get(groundSt).slipperiness * 0.91f;
            }

            float sneakScale = sneaking ? 0.30f : 1.0f;
            float accel = onGround
                ? (0.10f * sneakScale * (0.16277136f / (slipperiness * slipperiness * slipperiness)))
                : (0.02f * sneakScale);

            applyMoveFlying(strafeInput, forwardInput, accel);

            // Re-sample slipperiness after acceleration (exactorder)
            slipperiness = 0.91f;
            if (onGround) {
                uint16_t groundSt = world.getBlockAt(
                    static_cast<int>(std::floor(x)),
                    static_cast<int>(std::floor(y - 0.20f)),
                    static_cast<int>(std::floor(z))
                );
                slipperiness = world.registry.get(groundSt).slipperiness * 0.91f;
            }

            if (onLadder) {
                vx = std::clamp(vx, -0.15f, 0.15f);
                vz = std::clamp(vz, -0.15f, 0.15f);
                fallDistance = 0.0f;
                if (vy < -0.15f) vy = -0.15f;
                if (sneaking && vy < 0.0f) vy = 0.0f;
            } else if (jumpInput && onGround) {
                vy = 0.42f;
                onGround = false;
            }

            float prevYBeforeMove = y;
            moveWithCollision(world, vx, vy, vz, sneaking && onGround, audio, assets);

            // Climbing ladder when colliding horizontally against it
            if (onLadder && isCollidedHorizontally) {
                vy = 0.20f;
            }

            // Fall distance & fall damage (EntityLiving.fall: ceil(fallDistance - 3.0))
            if (onGround) {
                if (fallDistance > 3.0f) {
                    float dmg = std::ceil(fallDistance - 3.0f);
                    if (dmg > 0.0f) damage(dmg, audio, assets);
                }
                fallDistance = 0.0f;
            } else if (y < prevYBeforeMove && !onLadder && !inWeb) {
                fallDistance += (prevYBeforeMove - y);
            }

            vy -= 0.08f;
            vy *= 0.98f;
            vx *= slipperiness;
            vz *= slipperiness;
        }

        // ExactEntityPlayer.onLivingUpdate view bobbing calculation
        float horizMoved = std::sqrt((x - prevX) * (x - prevX) + (z - prevZ) * (z - prevZ));
        float targetBob  = onGround ? std::min(0.1f, horizMoved) : 0.0f;
        cameraYaw += (targetBob - cameraYaw) * 0.4f;
        if (onGround) walkDist += horizMoved * 0.6f;
    }

    // ========================================================================
    //World.rayTraceBlocks + Block.collisionRayTrace (Sub-Voxel 1/16)
    // ========================================================================
    static RaycastHit raycast(const WorldMap& world, float ox, float oy, float oz,
                              float dx, float dy, float dz, float maxDist) {
        RaycastHit out{};
        int bx = static_cast<int>(std::floor(ox));
        int by = static_cast<int>(std::floor(oy));
        int bz = static_cast<int>(std::floor(oz));

        int stepX = (dx > 0.0f) ? 1 : ((dx < 0.0f) ? -1 : 0);
        int stepY = (dy > 0.0f) ? 1 : ((dy < 0.0f) ? -1 : 0);
        int stepZ = (dz > 0.0f) ? 1 : ((dz < 0.0f) ? -1 : 0);

        float tDeltaX = (stepX != 0) ? std::abs(1.0f / dx) : 1e30f;
        float tDeltaY = (stepY != 0) ? std::abs(1.0f / dy) : 1e30f;
        float tDeltaZ = (stepZ != 0) ? std::abs(1.0f / dz) : 1e30f;

        float tMaxX = (stepX > 0) ? ((bx + 1.0f - ox) * tDeltaX) : ((ox - bx) * tDeltaX);
        float tMaxY = (stepY > 0) ? ((by + 1.0f - oy) * tDeltaY) : ((oy - by) * tDeltaY);
        float tMaxZ = (stepZ > 0) ? ((bz + 1.0f - oz) * tDeltaZ) : ((oz - bz) * tDeltaZ);

        SubBoxList subs;
        float dist = 0.0f;

        for (int i = 0; i < 128 && dist <= maxDist; ++i) {
            if (by >= 0 && by < CHUNK_HEIGHT) {
                uint16_t st = world.getBlockAt(bx, by, bz);
                const BlockDef& def = world.registry.get(st);
                if (def.shape != SHAPE_NONE && def.shape != SHAPE_FLUID) {
                    float bestSubDist = maxDist + 1.0f;
                    int bestNx = 0, bestNy = 0, bestNz = 0;
                    bool hitBlock = false;

                    auto testBox = [&](const AABB& box) {
                        float dHit = 0.0f;
                        int hnx = 0, hny = 0, hnz = 0;
                        if (box.calculateIntercept(ox, oy, oz, dx, dy, dz, maxDist, dHit, hnx, hny, hnz)) {
                            if (dHit < bestSubDist) {
                                bestSubDist = dHit;
                                bestNx = hnx; bestNy = hny; bestNz = hnz;
                                hitBlock = true;
                            }
                        }
                    };

                    if (def.shape == SHAPE_CUBE || def.shape == SHAPE_FIRE || def.shape == SHAPE_PORTAL) {
                        testBox({ static_cast<float>(bx), static_cast<float>(by), static_cast<float>(bz),
                                  bx + 1.0f, by + 1.0f, bz + 1.0f });
                    } else if (def.shape == SHAPE_TORCH) {
                        uint8_t meta = static_cast<uint8_t>(st & 0x0F);
                        if (meta == 1)      testBox({ bx + 0.0f, by + 0.2f, bz + 0.35f, bx + 0.3f, by + 0.8f, bz + 0.65f });
                        else if (meta == 2) testBox({ bx + 0.7f, by + 0.2f, bz + 0.35f, bx + 1.0f, by + 0.8f, bz + 0.65f });
                        else if (meta == 3) testBox({ bx + 0.35f, by + 0.2f, bz + 0.0f, bx + 0.65f, by + 0.8f, bz + 0.3f });
                        else if (meta == 4) testBox({ bx + 0.35f, by + 0.2f, bz + 0.7f, bx + 0.65f, by + 0.8f, bz + 1.0f });
                        else                testBox({ bx + 0.4f, by + 0.0f, bz + 0.4f, bx + 0.6f, by + 0.6f, bz + 0.6f });
                    } else if (def.shape == SHAPE_CROSS) {
                        testBox({ bx + 0.1f, by + 0.0f, bz + 0.1f, bx + 0.9f, by + 0.8f, bz + 0.9f });
                    } else if (def.shape == SHAPE_CROP) {
                        testBox({ bx + 0.0f, by + 0.0f, bz + 0.0f, bx + 1.0f, by + 0.25f, bz + 1.0f });
                    } else {
                        world.registry.getSubBoxes(st, bx, by, bz, [&](int qx, int qy, int qz) {
                            return world.getBlockAt(qx, qy, qz);
                        }, subs);
                        for (int b = 0; b < subs.count; ++b) {
                            const SubBox& sb = subs.boxes[b];
                            testBox({ bx + sb.minX * 0.0625f, by + sb.minY * 0.0625f, bz + sb.minZ * 0.0625f,
                                      bx + sb.maxX * 0.0625f, by + sb.maxY * 0.0625f, bz + sb.maxZ * 0.0625f });
                        }
                    }

                    if (hitBlock && bestSubDist <= maxDist) {
                        out.hit = true;
                        out.bx = bx; out.by = by; out.bz = bz;
                        out.nx = bestNx; out.ny = bestNy; out.nz = bestNz;
                        out.distance = bestSubDist;
                        return out;
                    }
                }
            }

            if (tMaxX < tMaxY && tMaxX < tMaxZ) {
                dist = tMaxX; tMaxX += tDeltaX; bx += stepX;
            } else if (tMaxY < tMaxZ) {
                dist = tMaxY; tMaxY += tDeltaY; by += stepY;
            } else {
                dist = tMaxZ; tMaxZ += tDeltaZ; bz += stepZ;
            }
        }
        return out;
    }

    // ========================================================================
    //Block.blockStrength & EntityPlayer.getCurrentPlayerStrVsBlock
    // ========================================================================
    float computeBlockDamagePerTick(const WorldMap& world, const BetaAssetManager& assets,
                                    int bx, int by, int bz, uint16_t heldId) const {
        uint16_t st = world.getBlockAt(bx, by, bz);
        uint8_t id  = static_cast<uint8_t>((st >> 4) & 0xFF);
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
        //ItemShears & ItemSword special block speed overrides
        if (heldId == ITEM_SHEARS) {
            if (id == 30 || id == 18) speed = 15.0f;
            else if (id == 35)        speed = 5.0f;
        } else if (tool.toolType == TOOL_SWORD && id == 30) {
            speed = 15.0f;
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
        uint8_t id   = static_cast<uint8_t>((brokenState >> 4) & 0xFF);
        uint8_t meta = static_cast<uint8_t>(brokenState & 0x0F);
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
            case 74: outId = ITEM_REDSTONE; outCount = static_cast<uint8_t>(4 + (rngSeed % 2)); break;
            case 21: outId = ITEM_DYE; outMeta = 4; outCount = static_cast<uint8_t>(4 + (rngSeed % 5)); break;
            case 18:
                if (heldId == ITEM_SHEARS) { outId = 18; outMeta = meta & 3; outCount = 1; }
                else if ((rngSeed % 20) == 0) { outId = 6; outMeta = meta & 3; outCount = 1; }
                break;
            case 30: outId = ITEM_STRING; outCount = 1; break;
            case 31:
                if (heldId == ITEM_SHEARS) { outId = 31; outMeta = meta; outCount = 1; }
                else if ((rngSeed % 8) == 0) { outId = ITEM_SEEDS; outCount = 1; }
                break;
            case 47: outId = ITEM_BOOK; outCount = 3; break;
            case 82: outId = ITEM_CLAY_BALL; outCount = 4; break;
            case 89: outId = ITEM_GLOWSTONE_DUST; outCount = static_cast<uint8_t>(2 + (rngSeed % 3)); break;
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
    void applyMoveFlying(float strafeInput, float forwardInput, float accel) {
        float dist = std::sqrt(strafeInput * strafeInput + forwardInput * forwardInput);
        if (dist >= 0.01f) {
            if (dist < 1.0f) dist = 1.0f;
            float scale  = accel / dist;
            float s      = strafeInput * scale;
            float f      = forwardInput * scale;
            float yawRad = yaw * 0.0174532925f;
            float sinYaw = std::sin(yawRad);
            float cosYaw = std::cos(yawRad);
            vx += cosYaw * f - sinYaw * s;
            vz += sinYaw * f + cosYaw * s;
        }
    }

    static bool checkMaterialInAABB(const WorldMap& world, const AABB& box, uint8_t idA, uint8_t idB) {
        int x0 = static_cast<int>(std::floor(box.minX)), x1 = static_cast<int>(std::floor(box.maxX));
        int y0 = static_cast<int>(std::floor(box.minY)), y1 = static_cast<int>(std::floor(box.maxY));
        int z0 = static_cast<int>(std::floor(box.minZ)), z1 = static_cast<int>(std::floor(box.maxZ));
        for (int wy = y0; wy <= y1; ++wy) {
            if (wy < 0 || wy >= CHUNK_HEIGHT) continue;
            for (int wz = z0; wz <= z1; ++wz) {
                for (int wx = x0; wx <= x1; ++wx) {
                    uint8_t id = static_cast<uint8_t>((world.getBlockAt(wx, wy, wz) >> 4) & 0xFF);
                    if (id == idA || id == idB) return true;
                }
            }
        }
        return false;
    }

    static bool checkWaterAndFlow(const WorldMap& world, const AABB& box, Vec3& outFlow) {
        int x0 = static_cast<int>(std::floor(box.minX)), x1 = static_cast<int>(std::floor(box.maxX));
        int y0 = static_cast<int>(std::floor(box.minY)), y1 = static_cast<int>(std::floor(box.maxY));
        int z0 = static_cast<int>(std::floor(box.minZ)), z1 = static_cast<int>(std::floor(box.maxZ));

        bool foundWater = false;
        Vec3 flowSum{ 0.0f, 0.0f, 0.0f };
        static constexpr int hDirs[4][2] = { {0, -1}, {-1, 0}, {0, 1}, {1, 0} };

        for (int wy = y0; wy <= y1; ++wy) {
            if (wy < 0 || wy >= CHUNK_HEIGHT) continue;
            for (int wz = z0; wz <= z1; ++wz) {
                for (int wx = x0; wx <= x1; ++wx) {
                    uint16_t st = world.getBlockAt(wx, wy, wz);
                    uint8_t id  = static_cast<uint8_t>((st >> 4) & 0xFF);
                    if (id != 8 && id != 9) continue;

                    uint8_t meta = static_cast<uint8_t>(st & 0x0F);
                    int effDecay = (meta >= 8) ? 0 : meta;
                    float waterTop = static_cast<float>(wy + 1) - (static_cast<float>(effDecay + 1) / 9.0f);
                    if (box.maxY < waterTop) continue;

                    foundWater = true;
                    for (const auto& d : hDirs) {
                        uint16_t nSt = world.getBlockAt(wx + d[0], wy, wz + d[1]);
                        uint8_t  nId = static_cast<uint8_t>((nSt >> 4) & 0xFF);
                        if (nId == 8 || nId == 9) {
                            uint8_t nMeta = static_cast<uint8_t>(nSt & 0x0F);
                            int nDecay = (nMeta >= 8) ? 0 : nMeta;
                            int diff = nDecay - effDecay;
                            flowSum.x += static_cast<float>(d[0] * diff);
                            flowSum.z += static_cast<float>(d[1] * diff);
                        } else if (!world.registry.getById(nId).isSolid) {
                            uint16_t downSt = world.getBlockAt(wx + d[0], wy - 1, wz + d[1]);
                            uint8_t  downId = static_cast<uint8_t>((downSt >> 4) & 0xFF);
                            if (downId == 8 || downId == 9) {
                                int diff = (8 - effDecay);
                                flowSum.x += static_cast<float>(d[0] * diff);
                                flowSum.z += static_cast<float>(d[1] * diff);
                            }
                        }
                    }
                    if (meta >= 8) {
                        flowSum.y -= 6.0f;
                    }
                }
            }
        }

        if (flowSum.lengthSq() > 1e-6f) {
            outFlow = flowSum.normalized();
        }
        return foundWater;
    }

    static void collectCollisionBoxes(const WorldMap& world, const AABB& queryBox, std::vector<AABB>& outBoxes) {
        outBoxes.clear();
        int x0 = static_cast<int>(std::floor(queryBox.minX));
        int x1 = static_cast<int>(std::floor(queryBox.maxX));
        int y0 = static_cast<int>(std::floor(queryBox.minY - 1.0f)); // -1.0f catches 1.5-tall Fences below
        int y1 = static_cast<int>(std::floor(queryBox.maxY));
        int z0 = static_cast<int>(std::floor(queryBox.minZ));
        int z1 = static_cast<int>(std::floor(queryBox.maxZ));

        SubBoxList subs;
        for (int wy = y0; wy <= y1; ++wy) {
            if (wy < 0 || wy >= CHUNK_HEIGHT) continue;
            for (int wz = z0; wz <= z1; ++wz) {
                for (int wx = x0; wx <= x1; ++wx) {
                    uint16_t st = world.getBlockAt(wx, wy, wz);
                    const BlockDef& def = world.registry.get(st);
                    if (!def.isSolid) continue;

                    uint8_t id = static_cast<uint8_t>((st >> 4) & 0xFF);
                    if (id == 85) {
                        //BlockFence.getCollisionBoundingBoxFromPool: 1.5 blocks tall!
                        AABB fenceBox{ static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz),
                                       wx + 1.0f, wy + 1.5f, wz + 1.0f };
                        if (queryBox.intersects(fenceBox)) outBoxes.push_back(fenceBox);
                    } else if (id == 88) {
                        //BlockSoulSand.getCollisionBoundingBoxFromPool: 14/16 (0.875) tall
                        AABB soulBox{ static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz),
                                      wx + 1.0f, wy + 0.875f, wz + 1.0f };
                        if (queryBox.intersects(soulBox)) outBoxes.push_back(soulBox);
                    } else if (def.shape == SHAPE_CUBE) {
                        AABB blk{ static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz),
                                  wx + 1.0f, wy + 1.0f, wz + 1.0f };
                        if (queryBox.intersects(blk)) outBoxes.push_back(blk);
                    } else {
                        world.registry.getSubBoxes(st, wx, wy, wz, [&](int qx, int qy, int qz) {
                            return world.getBlockAt(qx, qy, qz);
                        }, subs);
                        for (int b = 0; b < subs.count; ++b) {
                            const SubBox& sb = subs.boxes[b];
                            AABB blk{ wx + sb.minX * 0.0625f, wy + sb.minY * 0.0625f, wz + sb.minZ * 0.0625f,
                                      wx + sb.maxX * 0.0625f, wy + sb.maxY * 0.0625f, wz + sb.maxZ * 0.0625f };
                            if (queryBox.intersects(blk)) outBoxes.push_back(blk);
                        }
                    }
                }
            }
        }
    }

    bool collidesWithWorld(const WorldMap& world, const AABB& box) const {
        thread_local std::vector<AABB> testBoxes;
        collectCollisionBoxes(world, box, testBoxes);
        for (const AABB& b : testBoxes) {
            if (box.intersects(b)) return true;
        }
        return false;
    }

    // ExactEntity.moveEntity (3-axis AABB sweep + Sneak Edge-Hang + StepHeight 0.5F + Footsteps)
    void moveWithCollision(const WorldMap& world, float dx, float dy, float dz, bool edgeHang,
                           AudioEngine* audio = nullptr, const BetaAssetManager* assets = nullptr) {
        float h = sneaking ? SNEAK_HEIGHT : STAND_HEIGHT;
        AABB curBox = AABB::fromFootPos(x, y, z, HALF_WIDTH, h);

        // 1.Sneak Edge-Hang (0.05D step-back on X, Z, and combined diagonal XZ)
        if (edgeHang) {
            const float step = 0.05f;
            while (dx != 0.0f && !collidesWithWorld(world, curBox.offset(dx, -1.0f, 0.0f))) {
                if (std::abs(dx) <= step) { dx = 0.0f; break; }
                dx += (dx > 0.0f) ? -step : step;
            }
            while (dz != 0.0f && !collidesWithWorld(world, curBox.offset(0.0f, -1.0f, dz))) {
                if (std::abs(dz) <= step) { dz = 0.0f; break; }
                dz += (dz > 0.0f) ? -step : step;
            }
            while (dx != 0.0f && dz != 0.0f && !collidesWithWorld(world, curBox.offset(dx, -1.0f, dz))) {
                if (std::abs(dx) <= step) dx = 0.0f;
                else dx += (dx > 0.0f) ? -step : step;
                if (std::abs(dz) <= step) dz = 0.0f;
                else dz += (dz > 0.0f) ? -step : step;
            }
        }

        float origDx = dx, origDy = dy, origDz = dz;
        AABB origBox = curBox;

        thread_local std::vector<AABB> boxes;
        collectCollisionBoxes(world, curBox.addCoord(dx, dy, dz), boxes);

        // 2. Resolve Y offset
        for (const AABB& b : boxes) dy = b.calculateYOffset(curBox, dy);
        curBox = curBox.offset(0.0f, dy, 0.0f);

        bool wasOnGround = onGround || (origDy != dy && origDy < 0.0f);

        // 3. Resolve X then Z offset
        for (const AABB& b : boxes) dx = b.calculateXOffset(curBox, dx);
        curBox = curBox.offset(dx, 0.0f, 0.0f);

        for (const AABB& b : boxes) dz = b.calculateZOffset(curBox, dz);
        curBox = curBox.offset(0.0f, 0.0f, dz);

        // 4.Step-Up (stepHeight = 0.5F)
        if (STEP_HEIGHT > 0.0f && wasOnGround && (origDx != dx || origDz != dz)) {
            float savedDx = dx, savedDy = dy, savedDz = dz;
            AABB  savedBox = curBox;

            dx = origDx;
            dy = STEP_HEIGHT;
            dz = origDz;
            curBox = origBox;

            collectCollisionBoxes(world, curBox.addCoord(dx, dy, dz), boxes);

            for (const AABB& b : boxes) dy = b.calculateYOffset(curBox, dy);
            curBox = curBox.offset(0.0f, dy, 0.0f);

            for (const AABB& b : boxes) dx = b.calculateXOffset(curBox, dx);
            curBox = curBox.offset(dx, 0.0f, 0.0f);

            for (const AABB& b : boxes) dz = b.calculateZOffset(curBox, dz);
            curBox = curBox.offset(0.0f, 0.0f, dz);

            float dropDy = -STEP_HEIGHT;
            for (const AABB& b : boxes) dropDy = b.calculateYOffset(curBox, dropDy);
            curBox = curBox.offset(0.0f, dropDy, 0.0f);
            dy += dropDy;

            if (savedDx * savedDx + savedDz * savedDz >= dx * dx + dz * dz) {
                dx = savedDx;
                dy = savedDy;
                dz = savedDz;
                curBox = savedBox;
            }
        }

        // 5. Commit new coordinates & collision flags
        float oldX = x, oldZ = z;
        x = (curBox.minX + curBox.maxX) * 0.5f;
        y = curBox.minY;
        z = (curBox.minZ + curBox.maxZ) * 0.5f;

        isCollidedHorizontally = (origDx != dx) || (origDz != dz);
        isCollidedVertically   = (origDy != dy);
        onGround               = isCollidedVertically && (origDy < 0.0f);

        if (origDx != dx) vx = 0.0f;
        if (origDy != dy) vy = 0.0f;
        if (origDz != dz) vz = 0.0f;

        // 6.Footstep Sound Cadence (Entity.moveEntity)
        float stepDist = std::sqrt((x - oldX) * (x - oldX) + (z - oldZ) * (z - oldZ));
        if (onGround && !sneaking && !inWater) {
            distanceWalkedModified += stepDist * 0.6f;
            if (distanceWalkedModified > nextStepDistance) {
                nextStepDistance = static_cast<float>(static_cast<int>(distanceWalkedModified) + 1);
                if (audio && assets) {
                    uint16_t underSt = world.getBlockAt(
                        static_cast<int>(std::floor(x)),
                        static_cast<int>(std::floor(y - 0.2f)),
                        static_cast<int>(std::floor(z))
                    );
                    if (underSt != 0) {
                        SoundMaterial sm = world.registry.get(underSt).soundMat;
                        if (!assets->stepSounds[sm].samples.empty()) {
                            audio->play2D(assets->stepSounds[sm], 0.15f, 1.0f);
                        }
                    }
                }
            }
        }
    }
};