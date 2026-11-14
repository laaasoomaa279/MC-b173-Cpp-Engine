#pragma once
#include "platform.hpp"
#include "blocks.hpp"
#include "assets/asset_loader.hpp"
#include "assets/recipes_b173.hpp"
#include "world/world.hpp"
#include "gameplay/physics.hpp"
#include "gameplay/redstone_pistons.hpp"
#include "gameplay/combat_nether.hpp"

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================================
// Minecraft Beta 1.7.3 Classic Mobs, AI, Drops & Smooth Head/Body Tracking (1:1)
// ============================================================================

enum BetaMobType : uint8_t {
    MOB_ZOMBIE   = 0,
    MOB_CREEPER  = 1,
    MOB_SKELETON = 2,
    MOB_SPIDER   = 3,
    MOB_PIG      = 4,
    MOB_SHEEP    = 5,
    MOB_COW      = 6,
    MOB_GHAST    = 7
};

struct MobEntity {
    BetaMobType type         = MOB_ZOMBIE;
    float       x = 0.0f, y = 0.0f, z = 0.0f;
    float       prevX = 0.0f, prevY = 0.0f, prevZ = 0.0f;
    float       vx = 0.0f, vy = 0.0f, vz = 0.0f;

    // Mojang EntityLiving separated body & head angles (smooth turning!)
    float       yaw          = 0.0f; // Target movement yaw
    float       bodyYaw      = 0.0f; // Render body yaw (follows smoothly)
    float       prevBodyYaw  = 0.0f;
    float       headYaw      = 0.0f; // Head look yaw
    float       prevHeadYaw  = 0.0f;
    float       pitch        = 0.0f;
    float       prevPitch    = 0.0f;

    float       health       = 20.0f;
    float       maxHealth    = 20.0f;
    int         hurtTime     = 0;
    int         deathTime    = 0;
    int         ageTicks     = 0;
    int         attackCool   = 0;
    int         aiTimer      = 0;
    float       moveForward  = 0.0f;
    float       limbSwing    = 0.0f;
    float       limbSwingAmt = 0.0f;
    bool        onGround     = false;

    int         creeperFuse  = 0;
    bool        sheared      = false;
    uint8_t     woolColor    = 0;
    int         panicTicks   = 0;
};

struct DroppedItemEntity {
    ItemStack stack{};
    float     x = 0.0f, y = 0.0f, z = 0.0f;
    float     prevX = 0.0f, prevY = 0.0f, prevZ = 0.0f;
    float     vx = 0.0f, vy = 0.0f, vz = 0.0f;
    int       ageTicks    = 0;
    int       pickupDelay = 10;
    float     bobPhase    = 0.0f;
};

struct BlockParticle {
    float    x = 0.0f, y = 0.0f, z = 0.0f;
    float    vx = 0.0f, vy = 0.0f, vz = 0.0f;
    uint16_t spriteIdx = 0;
    float    size      = 0.12f;
    int      lifeTicks = 20;
};

class BetaEntityManager {
public:
    std::vector<MobEntity>         mobs;
    std::vector<DroppedItemEntity> droppedItems;
    std::vector<BlockParticle>     particles;
    uint32_t                       rng = 0x9E3779B9u;

    static float wrapDegrees(float deg) {
        while (deg >= 180.0f) deg -= 360.0f;
        while (deg < -180.0f) deg += 360.0f;
        return deg;
    }

    static float approachAngle(float current, float target, float maxStep) {
        float diff = wrapDegrees(target - current);
        diff = std::clamp(diff, -maxStep, maxStep);
        return wrapDegrees(current + diff);
    }

    float nextFloat() {
        rng = rng * 1664525u + 1013904223u;
        return (float)(rng >> 8) / 16777216.0f;
    }

    void spawnMob(BetaMobType type, float x, float y, float z) {
        if (mobs.size() >= 64) return;
        MobEntity m{};
        m.type  = type;
        m.x = m.prevX = x;
        m.y = m.prevY = y;
        m.z = m.prevZ = z;
        m.yaw = m.bodyYaw = m.prevBodyYaw = m.headYaw = m.prevHeadYaw = nextFloat() * 360.0f - 180.0f;

        switch (type) {
            case MOB_ZOMBIE:   m.health = m.maxHealth = 20.0f; break;
            case MOB_CREEPER:  m.health = m.maxHealth = 20.0f; break;
            case MOB_SKELETON: m.health = m.maxHealth = 20.0f; break;
            case MOB_SPIDER:   m.health = m.maxHealth = 20.0f; break;
            case MOB_PIG:      m.health = m.maxHealth = 10.0f; break;
            case MOB_SHEEP:
                m.health = m.maxHealth = 8.0f;
                {
                    float r = nextFloat();
                    if (r < 0.05f)       m.woolColor = 15;
                    else if (r < 0.10f)  m.woolColor = 7;
                    else if (r < 0.15f)  m.woolColor = 8;
                    else if (r < 0.18f)  m.woolColor = 12;
                    else if (r < 0.182f) m.woolColor = 6;
                    else                 m.woolColor = 0;
                }
                break;
            case MOB_COW:      m.health = m.maxHealth = 10.0f; break;
            case MOB_GHAST:    m.health = m.maxHealth = 10.0f; break;
        }
        mobs.push_back(m);
    }

    void spawnDroppedItem(uint16_t id, uint16_t damage, uint8_t count, float x, float y, float z) {
        if (id == 0 || count == 0 || droppedItems.size() >= 256) return;
        DroppedItemEntity d{};
        d.stack = { id, damage, count };
        d.x = d.prevX = x;
        d.y = d.prevY = y;
        d.z = d.prevZ = z;
        d.vx = (nextFloat() - 0.5f) * 0.20f;
        d.vy = 0.20f;
        d.vz = (nextFloat() - 0.5f) * 0.20f;
        d.bobPhase = nextFloat() * 6.2831853f;
        droppedItems.push_back(d);
    }

    void spawnBlockBreakParticles(const WorldMap& world, int bx, int by, int bz, uint16_t state) {
        if (state == 0) return;
        uint16_t spr = world.registry.resolveFaceTexture(state, FACE_NORTH);
        for (int i = 0; i < 14 && particles.size() < 512; ++i) {
            BlockParticle p{};
            p.x = bx + 0.15f + nextFloat() * 0.7f;
            p.y = by + 0.15f + nextFloat() * 0.7f;
            p.z = bz + 0.15f + nextFloat() * 0.7f;
            p.vx = (nextFloat() - 0.5f) * 0.25f;
            p.vy = 0.10f + nextFloat() * 0.20f;
            p.vz = (nextFloat() - 0.5f) * 0.25f;
            p.spriteIdx = spr;
            p.size = 0.08f + nextFloat() * 0.06f;
            p.lifeTicks = 12 + (int)(nextFloat() * 14.0f);
            particles.push_back(p);
        }
    }

    bool performPlayerMeleeAttack(PlayerPhysics& player, const BetaAssetManager& assets,
                                  uint16_t heldId, const Vec3& eyePos, const Vec3& lookDir,
                                  AudioEngine* audio = nullptr) {
        player.triggerSwing();
        float reach = 3.6f;
        int bestMob = -1;
        float bestDist = reach;

        for (size_t i = 0; i < mobs.size(); ++i) {
            MobEntity& m = mobs[i];
            if (m.health <= 0.0f) continue;
            float halfW  = (m.type == MOB_SPIDER) ? 0.75f : ((m.type == MOB_GHAST) ? 2.0f : 0.45f);
            float height = (m.type == MOB_SPIDER) ? 0.95f : ((m.type == MOB_PIG) ? 0.95f : ((m.type == MOB_GHAST) ? 4.0f : 1.95f));

            Vec3 center{ m.x, m.y + height * 0.5f, m.z };
            Vec3 toMob = center - eyePos;
            float proj = toMob.dot(lookDir);
            if (proj < 0.0f || proj > bestDist) continue;

            Vec3 closest = eyePos + lookDir * proj;
            float dx = std::abs(closest.x - m.x);
            float dy = std::abs(closest.y - (m.y + height * 0.5f));
            float dz = std::abs(closest.z - m.z);
            if (dx <= halfW + 0.2f && dy <= height * 0.55f + 0.2f && dz <= halfW + 0.2f) {
                bestDist = proj;
                bestMob = (int)i;
            }
        }

        if (bestMob < 0) return false;

        MobEntity& target = mobs[(size_t)bestMob];
        if (target.hurtTime > 0) return false;

        float dmg = assets.getItemDef(heldId).attackDamage;
        target.health -= dmg;
        target.hurtTime = 10;

        float yawRad = player.yaw * 0.0174532925f;
        target.vx += std::cos(yawRad) * 0.35f;
        target.vy  = 0.32f;
        target.vz += std::sin(yawRad) * 0.35f;

        if (target.type == MOB_PIG || target.type == MOB_SHEEP || target.type == MOB_COW) {
            target.panicTicks = 60;
        }

        if (audio && !assets.hitSound.samples.empty()) {
            audio->play3D(assets.hitSound, 1.0f, 1.0f, {target.x, target.y, target.z}, eyePos, player.yaw, true);
        }

        if (target.health <= 0.0f) {
            dropMobLoot(target);
        }
        return true;
    }

    template<typename GiveItemFn, typename QueueType>
    void tickAll(WorldMap& world, PlayerPhysics& player, BetaCombatAndNetherSystem& combat,
                 BetaRedstonePistonEngine& redstone, int difficulty,
                 GiveItemFn&& giveItemToPlayer, QueueType& meshQueue,
                 AudioEngine* audio = nullptr, const BetaAssetManager* assets = nullptr) {
        if ((world.worldTick % 40) == 0 && mobs.size() < 28) {
            trySpawnNaturalMob(world, player, difficulty);
        }

        for (size_t i = 0; i < mobs.size(); ) {
            MobEntity& m = mobs[i];
            m.prevX = m.x; m.prevY = m.y; m.prevZ = m.z;
            m.prevBodyYaw = m.bodyYaw;
            m.prevHeadYaw = m.headYaw;
            m.prevPitch   = m.pitch;
            m.ageTicks++;

            // Death animation (20 ticks sideways fall before despawn, exact Mojang EntityLiving)
            if (m.health <= 0.0f) {
                if (++m.deathTime >= 20) {
                    mobs[i] = mobs.back();
                    mobs.pop_back();
                } else {
                    ++i;
                }
                continue;
            }

            if (m.hurtTime > 0) m.hurtTime--;
            if (m.attackCool > 0) m.attackCool--;
            if (m.panicTicks > 0) m.panicTicks--;

            float dxP = player.x - m.x;
            float dzP = player.z - m.z;
            float distSq = dxP * dxP + dzP * dzP;

            if (distSq > 96.0f * 96.0f) {
                mobs[i] = mobs.back();
                mobs.pop_back();
                continue;
            }

            if (world.dimension == 0 && (m.type == MOB_ZOMBIE || m.type == MOB_SKELETON)) {
                int bx = (int)std::floor(m.x), by = (int)std::floor(m.y + 1.0f), bz = (int)std::floor(m.z);
                if (world.getSkyBrightness() > 0.75f && world.getSkyLightAt(bx, by, bz) >= 14 && (world.worldTick % 20) == 0) {
                    m.health -= 2.0f;
                    m.hurtTime = 8;
                    if (m.health <= 0.0f) { dropMobLoot(m); ++i; continue; }
                }
            }

            bool isHostile = (m.type == MOB_ZOMBIE || m.type == MOB_CREEPER || m.type == MOB_SKELETON || m.type == MOB_SPIDER || m.type == MOB_GHAST);
            float dist = std::sqrt(distSq);

            if (isHostile && difficulty > 0 && !player.flying && dist < 16.0f) {
                float targetYaw = std::atan2(-dxP, dzP) * 57.2957795f;
                m.yaw     = approachAngle(m.yaw, targetYaw, 22.0f);
                m.headYaw = approachAngle(m.headYaw, targetYaw, 30.0f);
                m.pitch   = std::clamp(std::atan2((player.y + 1.2f) - (m.y + 1.4f), std::max(0.1f, dist)) * 57.2957795f, -40.0f, 40.0f);
                m.moveForward = 0.034f;

                if (m.type == MOB_CREEPER) {
                    if (dist < 3.0f) {
                        m.moveForward = 0.01f;
                        if (m.creeperFuse == 0 && audio && assets && !assets->fizzSound.samples.empty()) {
                            audio->play3D(assets->fizzSound, 1.0f, 0.5f, {m.x, m.y, m.z}, {player.x, player.y, player.z}, player.yaw, true);
                        }
                        if (++m.creeperFuse >= 30) {
                            BetaRedstonePistonEngine::detonateExplosion(world, m.x, m.y + 0.5f, m.z, 3.0f, meshQueue);
                            if (audio && assets && !assets->explodeSound.samples.empty()) {
                                audio->play3D(assets->explodeSound, 1.2f, 1.0f, {m.x, m.y, m.z}, {player.x, player.y, player.z}, player.yaw, true);
                            }
                            if (dist < 5.0f) player.damage((5.0f - dist) * 4.5f, audio, assets);
                            m.health = 0.0f;
                            m.deathTime = 20;
                            continue;
                        }
                    } else {
                        m.creeperFuse = std::max(0, m.creeperFuse - 1);
                    }
                } else if (m.type == MOB_SKELETON) {
                    if (dist < 10.0f) m.moveForward = 0.015f;
                    if (m.attackCool == 0 && dist < 14.0f) {
                        float shootYaw = std::atan2(dzP, dxP) * 57.2957795f;
                        float shootPitch = std::atan2((player.y + 1.1f) - (m.y + 1.4f), dist) * 57.2957795f + 4.0f;
                        combat.spawnProjectile(PROJ_ARROW, false, (int)i, {m.x, m.y + 1.5f, m.z}, shootYaw, shootPitch, 1.35f);
                        if (audio && assets && !assets->bowSound.samples.empty()) {
                            audio->play3D(assets->bowSound, 0.9f, 1.0f, {m.x, m.y, m.z}, {player.x, player.y, player.z}, player.yaw, true);
                        }
                        m.attackCool = 45;
                    }
                } else {
                    if (dist < 1.55f && std::abs(player.y - m.y) < 1.8f && m.attackCool == 0) {
                        float dmg = (difficulty == 1) ? 2.0f : ((difficulty == 2) ? 4.0f : 6.0f);
                        player.damage(dmg, audio, assets);
                        m.attackCool = 20;
                    }
                }
            } else {
                if (--m.aiTimer <= 0) {
                    m.aiTimer = 30 + (int)(nextFloat() * 60.0f);
                    if (nextFloat() < 0.65f) {
                        m.yaw = wrapDegrees(m.yaw + (nextFloat() - 0.5f) * 140.0f);
                        m.moveForward = (m.panicTicks > 0) ? 0.036f : 0.022f;
                    } else {
                        m.moveForward = 0.0f;
                    }
                }
                // Passive mobs glance curiously at nearby player
                if (dist < 6.0f) {
                    float lookAtPlayer = std::atan2(-dxP, dzP) * 57.2957795f;
                    m.headYaw = approachAngle(m.headYaw, lookAtPlayer, 15.0f);
                    m.pitch   = std::clamp(std::atan2((player.y + 1.2f) - (m.y + 1.0f), std::max(0.1f, dist)) * 57.2957795f, -30.0f, 30.0f);
                } else {
                    m.headYaw = approachAngle(m.headYaw, m.yaw, 12.0f);
                    m.pitch  *= 0.85f;
                }
            }

            // Smoothly align bodyYaw with movement direction (Mojang EntityLiving.updateDistance)
            float rad  = m.yaw * 0.0174532925f;
            float dirX = -std::sin(rad);
            float dirZ =  std::cos(rad);

            float accel = m.onGround ? m.moveForward : (m.moveForward * 0.25f);
            m.vx += dirX * accel;
            m.vz += dirZ * accel;

            // Swim upward in water
            uint8_t footBlk = (uint8_t)((world.getBlockAt((int)std::floor(m.x), (int)std::floor(m.y + 0.3f), (int)std::floor(m.z)) >> 4) & 0xFF);
            if (footBlk == 8 || footBlk == 9) {
                m.vy += 0.04f;
            } else if (m.onGround && m.moveForward > 0.005f) {
                int frontX = (int)std::floor(m.x + dirX * 0.75f);
                int frontY = (int)std::floor(m.y + 0.2f);
                int frontZ = (int)std::floor(m.z + dirZ * 0.75f);
                if (world.registry.get(world.getBlockAt(frontX, frontY, frontZ)).isSolid &&
                    !world.registry.get(world.getBlockAt(frontX, frontY + 1, frontZ)).isSolid) {
                    m.vy = 0.42f;
                    m.onGround = false;
                }
            }

            moveMobWithCollision(world, m);

            float drag = m.onGround ? 0.55f : 0.91f;
            m.vx *= drag;
            m.vz *= drag;
            m.vy = (m.vy - 0.08f) * 0.98f;

            float movedX = m.x - m.prevX;
            float movedZ = m.z - m.prevZ;
            float horizSpeed = std::sqrt(movedX * movedX + movedZ * movedZ);
            if (horizSpeed > 0.01f) {
                float moveAngle = std::atan2(-movedX, movedZ) * 57.2957795f;
                m.bodyYaw = approachAngle(m.bodyYaw, moveAngle, 18.0f);
            } else {
                // Clamp head-to-body twist within 75 degrees (Mojang exact limit)
                float headDiff = wrapDegrees(m.headYaw - m.bodyYaw);
                if (headDiff > 75.0f)  m.bodyYaw = wrapDegrees(m.headYaw - 75.0f);
                if (headDiff < -75.0f) m.bodyYaw = wrapDegrees(m.headYaw + 75.0f);
            }

            m.limbSwingAmt = std::min(1.0f, m.limbSwingAmt * 0.7f + horizSpeed * 2.5f);
            m.limbSwing   += horizSpeed * 2.8f;

            ++i;
        }

        // 3. Projectiles
        for (size_t i = 0; i < combat.projectiles.size(); ) {
            ProjectileEntity& pr = combat.projectiles[i];
            pr.lifeTicks++;
            if (pr.lifeTicks > 600) {
                combat.projectiles[i] = combat.projectiles.back();
                combat.projectiles.pop_back();
                continue;
            }
            if (!pr.inGround) {
                pr.x += pr.vx; pr.y += pr.vy; pr.z += pr.vz;
                pr.vy -= 0.03f;
                pr.vx *= 0.99f; pr.vy *= 0.99f; pr.vz *= 0.99f;
                pr.yaw   = std::atan2(pr.vz, pr.vx) * 57.2957795f;
                pr.pitch = std::atan2(pr.vy, std::sqrt(pr.vx * pr.vx + pr.vz * pr.vz)) * 57.2957795f;

                int bx = (int)std::floor(pr.x), by = (int)std::floor(pr.y), bz = (int)std::floor(pr.z);
                if (world.registry.get(world.getBlockAt(bx, by, bz)).isSolid) {
                    if (pr.kind == PROJ_ARROW) {
                        pr.inGround = true;
                        pr.vx = pr.vy = pr.vz = 0.0f;
                    } else {
                        combat.projectiles[i] = combat.projectiles.back();
                        combat.projectiles.pop_back();
                        continue;
                    }
                } else if (pr.fromPlayer) {
                    for (auto& m : mobs) {
                        if (m.health <= 0.0f) continue;
                        if (std::abs(pr.x - m.x) < 0.55f && pr.y >= m.y && pr.y <= m.y + 1.9f && std::abs(pr.z - m.z) < 0.55f) {
                            if (pr.kind == PROJ_ARROW) {
                                m.health -= 4.0f;
                                m.hurtTime = 10;
                                if (m.health <= 0.0f) dropMobLoot(m);
                            }
                            pr.lifeTicks = 9999;
                            break;
                        }
                    }
                } else {
                    if (std::abs(pr.x - player.x) < 0.45f && pr.y >= player.y && pr.y <= player.y + 1.8f && std::abs(pr.z - player.z) < 0.45f) {
                        player.damage(3.0f, audio, assets);
                        pr.lifeTicks = 9999;
                    }
                }
            } else if (pr.fromPlayer) {
                if (std::abs(pr.x - player.x) < 1.2f && std::abs(pr.y - player.y) < 1.5f && std::abs(pr.z - player.z) < 1.2f) {
                    if (giveItemToPlayer(ITEM_ARROW, 0, 1)) {
                        pr.lifeTicks = 9999;
                    }
                }
            }
            ++i;
        }

        // 4. Dropped Items
        for (size_t i = 0; i < droppedItems.size(); ) {
            DroppedItemEntity& d = droppedItems[i];
            d.prevX = d.x; d.prevY = d.y; d.prevZ = d.z;
            d.ageTicks++;
            if (d.pickupDelay > 0) d.pickupDelay--;

            if (d.ageTicks > 6000) {
                droppedItems[i] = droppedItems.back();
                droppedItems.pop_back();
                continue;
            }

            d.vy -= 0.04f;
            d.x += d.vx; d.y += d.vy; d.z += d.vz;
            int bx = (int)std::floor(d.x), by = (int)std::floor(d.y - 0.1f), bz = (int)std::floor(d.z);
            if (world.registry.get(world.getBlockAt(bx, by, bz)).isSolid) {
                d.y = std::floor(d.y - 0.1f) + 1.12f;
                d.vy = 0.0f;
                d.vx *= 0.6f; d.vz *= 0.6f;
            } else {
                d.vx *= 0.98f; d.vy *= 0.98f; d.vz *= 0.98f;
            }

            if (d.pickupDelay == 0) {
                float dx = player.x - d.x, dy = (player.y + 0.8f) - d.y, dz = player.z - d.z;
                if (dx * dx + dy * dy + dz * dz < 2.25f && player.health > 0.0f) {
                    if (giveItemToPlayer(d.stack.id, d.stack.damage, d.stack.count)) {
                        if (audio && assets && !assets->popSound.samples.empty()) {
                            audio->play2D(assets->popSound, 0.35f, 1.5f);
                        }
                        droppedItems[i] = droppedItems.back();
                        droppedItems.pop_back();
                        continue;
                    }
                }
            }
            ++i;
        }

        // 5. Particles
        for (size_t i = 0; i < particles.size(); ) {
            BlockParticle& p = particles[i];
            if (--p.lifeTicks <= 0) {
                particles[i] = particles.back();
                particles.pop_back();
                continue;
            }
            p.vy -= 0.04f;
            p.x += p.vx; p.y += p.vy; p.z += p.vz;
            p.vx *= 0.95f; p.vz *= 0.95f;
            ++i;
        }
        (void)redstone;
    }

private:
    void dropMobLoot(const MobEntity& m) {
        switch (m.type) {
            case MOB_ZOMBIE:   spawnDroppedItem(ITEM_FEATHER, 0, (uint8_t)(1 + (int)(nextFloat() * 2.0f)), m.x, m.y + 0.5f, m.z); break;
            case MOB_CREEPER:  spawnDroppedItem(ITEM_GUNPOWDER, 0, (uint8_t)(1 + (int)(nextFloat() * 2.0f)), m.x, m.y + 0.5f, m.z); break;
            case MOB_SKELETON:
                spawnDroppedItem(ITEM_ARROW, 0, (uint8_t)(1 + (int)(nextFloat() * 2.0f)), m.x, m.y + 0.5f, m.z);
                spawnDroppedItem(ITEM_BONE,  0, (uint8_t)(1 + (int)(nextFloat() * 2.0f)), m.x, m.y + 0.5f, m.z);
                break;
            case MOB_SPIDER:   spawnDroppedItem(ITEM_STRING, 0, (uint8_t)(1 + (int)(nextFloat() * 2.0f)), m.x, m.y + 0.5f, m.z); break;
            case MOB_PIG:      spawnDroppedItem(ITEM_PORKCHOP_RAW, 0, 1, m.x, m.y + 0.5f, m.z); break;
            case MOB_SHEEP:    if (!m.sheared) spawnDroppedItem(35, m.woolColor, 1, m.x, m.y + 0.5f, m.z); break;
            case MOB_COW:      spawnDroppedItem(ITEM_LEATHER, 0, (uint8_t)(1 + (int)(nextFloat() * 2.0f)), m.x, m.y + 0.5f, m.z); break;
            case MOB_GHAST:    spawnDroppedItem(ITEM_GUNPOWDER, 0, 2, m.x, m.y + 0.5f, m.z); break;
        }
    }

    void trySpawnNaturalMob(const WorldMap& world, const PlayerPhysics& player, int difficulty) {
        float ang = nextFloat() * 6.2831853f;
        float dist = 18.0f + nextFloat() * 24.0f;
        int wx = (int)std::floor(player.x + std::cos(ang) * dist);
        int wz = (int)std::floor(player.z + std::sin(ang) * dist);
        int wy = world.getTopSolidY(wx, wz);
        if (wy <= SEA_LEVEL || wy >= CHUNK_HEIGHT - 3) return;

        uint8_t groundId = (uint8_t)((world.getBlockAt(wx, wy - 1, wz) >> 4) & 0xFF);
        uint8_t skyL = world.getSkyLightAt(wx, wy, wz);
        uint8_t blkL = world.getBlockLightAt(wx, wy, wz);

        if (groundId == 2 && skyL >= 9 && nextFloat() < 0.4f) {
            int r = (int)(nextFloat() * 3.0f);
            BetaMobType animal = (r == 0) ? MOB_PIG : ((r == 1) ? MOB_SHEEP : MOB_COW);
            spawnMob(animal, wx + 0.5f, (float)wy, wz + 0.5f);
        } else if (difficulty > 0 && blkL < 8 && (skyL < 8 || world.getSkyBrightness() < 0.45f)) {
            int r = (int)(nextFloat() * 4.0f);
            BetaMobType monster = (r == 0) ? MOB_ZOMBIE : ((r == 1) ? MOB_CREEPER : ((r == 2) ? MOB_SKELETON : MOB_SPIDER));
            spawnMob(monster, wx + 0.5f, (float)wy, wz + 0.5f);
        }
    }

    void moveMobWithCollision(const WorldMap& world, MobEntity& m) const {
        float hw = (m.type == MOB_SPIDER) ? 0.65f : 0.35f;
        float ht = (m.type == MOB_SPIDER) ? 0.90f : 1.75f;

        auto collides = [&](float tx, float ty, float tz) -> bool {
            AABB box = AABB::fromFootPos(tx, ty, tz, hw, ht);
            int x0 = (int)std::floor(box.minX), x1 = (int)std::floor(box.maxX);
            int y0 = (int)std::floor(box.minY), y1 = (int)std::floor(box.maxY);
            int z0 = (int)std::floor(box.minZ), z1 = (int)std::floor(box.maxZ);
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z)
                    for (int x = x0; x <= x1; ++x)
                        if (world.registry.get(world.getBlockAt(x, y, z)).isSolid) return true;
            return false;
        };

        if (!collides(m.x, m.y + m.vy, m.z)) {
            m.y += m.vy;
            m.onGround = false;
        } else {
            if (m.vy < 0.0f) {
                m.onGround = true;
                m.y = std::floor(m.y + m.vy + 1.0f);
            }
            m.vy = 0.0f;
        }

        if (!collides(m.x + m.vx, m.y, m.z)) m.x += m.vx;
        else m.vx = 0.0f;

        if (!collides(m.x, m.y, m.z + m.vz)) m.z += m.vz;
        else m.vz = 0.0f;
    }
};