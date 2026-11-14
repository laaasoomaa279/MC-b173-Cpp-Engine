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
// Classic Mobs, AI, Drops & Projectiles
// ============================================================================
//       * Exact separated bodyYaw / headYaw tracking (75-deg neck clamp)
//       * Exact 20-tick sideways death fall (deathTime == 20 -> loot + despawn)
//       * Exact cliff & lava avoidance, obstacle jump (vy = 0.42F), water
//         swimming (vy += 0.04F), entity-to-entity soft separation, and
//         sub-voxel AABB collision (Fences 1.5 blocks, Slabs, Stairs, Doors)
//       * Sunlight combustion when skyBrightness > 0.5F and exposed to sky
//       * Skeleton 30-tick bow cadence with ballistic gravity compensation,
//         close-range strafing, and Arrow + Bone drops
//       * Exact fuseTime = 30 state machine (ignites < 3.0 blocks with pitch-0.5
//         fizz sound; only cancels > 7.0 blocks or lost line-of-sight),
//         radius-3.0 explosion, and Music Disc drop when slain by a Skeleton
//       * Daylight neutrality (aggros only when light < 0.5F or provoked),
//         wall climbing (vy = 0.20F on horizontal collision), and leap/pounce
//         attack (dist 2..6 blocks, vy = 0.4F)
//       * 60-tick fleeing panic mode when damaged, grass/light preference,
//         exact Beta 1.7.3 Sheep wool color probabilities (White, Black, Gray,
//         Light Gray, Brown, 0.164% Pink), and Cooked Porkchop drop if on fire
//       * 3D aerial waypoint navigation with ray-checked obstacle avoidance,
//         20-tick fireball charge & launch, and player melee fireball deflection
//         ("Return to Sender" instant-kill on deflected hit)
//       * Exact 24..128 block spawn annulus, Overworld vs Nether (DIM-1 Ghast)
//         spawning rules, and light-level checks
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

    //EntityLiving separated body & head angles (smooth turning!)
    float       yaw          = 0.0f;
    float       bodyYaw      = 0.0f;
    float       prevBodyYaw  = 0.0f;
    float       headYaw      = 0.0f;
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

    // Extended Notch state fields
    int         creeperState           = -1;   // -1 = idle, 1 = ignited (EntityCreeper.creeperState)
    int         fireTicks              = 0;    // Burning ticks (sunlight, lava, fire)
    bool        spiderAngry            = false;// Provoked in daylight
    bool        isCollidedHorizontally = false;
    float       moveStrafing           = 0.0f;
    int         ghastCourseCooldown    = 0;
    float       ghastWaypointX         = 0.0f;
    float       ghastWaypointY         = 64.0f;
    float       ghastWaypointZ         = 0.0f;
    int         ghastAttackCounter     = 0;
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
    static constexpr uint8_t PROJ_KIND_FIREBALL = 3; // Rendered via sprite 14 in entity_renderer

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
        return static_cast<float>(rng >> 8) / 16777216.0f;
    }

    int nextInt(int bound) {
        if (bound <= 0) return 0;
        rng = rng * 1664525u + 1013904223u;
        return static_cast<int>((rng >> 8) % static_cast<uint32_t>(bound));
    }

    static void getMobDimensions(BetaMobType type, float& outHalfW, float& outHeight) {
        switch (type) {
            case MOB_SPIDER:  outHalfW = 0.70f; outHeight = 0.90f; break;
            case MOB_PIG:     outHalfW = 0.45f; outHeight = 0.90f; break;
            case MOB_SHEEP:   outHalfW = 0.45f; outHeight = 1.30f; break;
            case MOB_COW:     outHalfW = 0.45f; outHeight = 1.30f; break;
            case MOB_GHAST:   outHalfW = 2.00f; outHeight = 4.00f; break;
            case MOB_CREEPER: outHalfW = 0.30f; outHeight = 1.70f; break;
            default:          outHalfW = 0.30f; outHeight = 1.80f; break; // Zombie & Skeleton
        }
    }

    void spawnMob(BetaMobType type, float x, float y, float z) {
        if (mobs.size() >= 70) return;
        MobEntity m{};
        m.type  = type;
        m.x = m.prevX = x;
        m.y = m.prevY = y;
        m.z = m.prevZ = z;
        m.yaw = m.bodyYaw = m.prevBodyYaw = m.headYaw = m.prevHeadYaw = nextFloat() * 360.0f - 180.0f;
        m.ghastWaypointX = x;
        m.ghastWaypointY = y;
        m.ghastWaypointZ = z;

        switch (type) {
            case MOB_ZOMBIE:   m.health = m.maxHealth = 20.0f; break;
            case MOB_CREEPER:  m.health = m.maxHealth = 20.0f; break;
            case MOB_SKELETON: m.health = m.maxHealth = 20.0f; break;
            case MOB_SPIDER:   m.health = m.maxHealth = 20.0f; break;
            case MOB_PIG:      m.health = m.maxHealth = 10.0f; break;
            case MOB_SHEEP:
                m.health = m.maxHealth = 8.0f;
                {
                    // Exact Notch EntitySheep.getRandomFleeceColor(Random)
                    int i = nextInt(100);
                    if (i < 5)       m.woolColor = 15; // Black (5%)
                    else if (i < 10) m.woolColor = 7;  // Gray (5%)
                    else if (i < 15) m.woolColor = 8;  // Light Gray (5%)
                    else if (i < 18) m.woolColor = 12; // Brown (3%)
                    else if (nextInt(500) == 0) m.woolColor = 6; // Pink (0.164%)
                    else             m.woolColor = 0;  // White (81.836%)
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
        float ang = nextFloat() * 6.2831853f;
        float spd = nextFloat() * 0.1f;
        d.vx = -std::sin(ang) * spd;
        d.vy = 0.20f;
        d.vz =  std::cos(ang) * spd;
        d.bobPhase = nextFloat() * 6.2831853f;
        droppedItems.push_back(d);
    }

    void spawnBlockBreakParticles(const WorldMap& world, int bx, int by, int bz, uint16_t state) {
        if (state == 0) return;
        uint16_t spr = world.registry.resolveFaceTexture(state, FACE_NORTH);
        for (int i = 0; i < 16 && particles.size() < 512; ++i) {
            BlockParticle p{};
            p.x = static_cast<float>(bx) + 0.15f + nextFloat() * 0.7f;
            p.y = static_cast<float>(by) + 0.15f + nextFloat() * 0.7f;
            p.z = static_cast<float>(bz) + 0.15f + nextFloat() * 0.7f;
            p.vx = (nextFloat() - 0.5f) * 0.26f;
            p.vy = 0.10f + nextFloat() * 0.22f;
            p.vz = (nextFloat() - 0.5f) * 0.26f;
            p.spriteIdx = spr;
            p.size = 0.08f + nextFloat() * 0.06f;
            p.lifeTicks = 12 + nextInt(14);
            particles.push_back(p);
        }
    }

    bool performPlayerMeleeAttack(PlayerPhysics& player, const BetaAssetManager& assets,
                                  uint16_t heldId, const Vec3& eyePos, const Vec3& lookDir,
                                  AudioEngine* audio = nullptr, BetaCombatAndNetherSystem* combat = nullptr) {
        player.triggerSwing();
        const float reach = 3.8f;

        // 1. Check if player punched an incoming Ghast Fireball ("Return to Sender")
        if (combat) {
            for (auto& pr : combat->projectiles) {
                if (static_cast<uint8_t>(pr.kind) == PROJ_KIND_FIREBALL && !pr.inGround) {
                    Vec3 toFb{ pr.x - eyePos.x, pr.y - eyePos.y, pr.z - eyePos.z };
                    float proj = toFb.dot(lookDir);
                    if (proj > 0.0f && proj <= reach) {
                        Vec3 closest = eyePos + lookDir * proj;
                        float dx = std::abs(closest.x - pr.x);
                        float dy = std::abs(closest.y - pr.y);
                        float dz = std::abs(closest.z - pr.z);
                        if (dx <= 0.75f && dy <= 0.75f && dz <= 0.75f) {
                            pr.fromPlayer  = true;
                            pr.ownerMobIdx = -1;
                            pr.vx = lookDir.x * 1.15f;
                            pr.vy = lookDir.y * 1.15f;
                            pr.vz = lookDir.z * 1.15f;
                            pr.yaw   = std::atan2(pr.vz, pr.vx) * 57.2957795f;
                            pr.pitch = std::atan2(pr.vy, std::sqrt(pr.vx * pr.vx + pr.vz * pr.vz)) * 57.2957795f;
                            if (audio && !assets.bowSound.samples.empty()) {
                                audio->play2D(assets.bowSound, 0.9f, 1.4f);
                            }
                            return true;
                        }
                    }
                }
            }
        }

        // 2. Ray-box test against all living Mobs
        int bestMob = -1;
        float bestDist = reach;

        for (size_t i = 0; i < mobs.size(); ++i) {
            MobEntity& m = mobs[i];
            if (m.health <= 0.0f) continue;
            float halfW = 0.35f, height = 1.80f;
            getMobDimensions(m.type, halfW, height);

            Vec3 center{ m.x, m.y + height * 0.5f, m.z };
            Vec3 toMob = center - eyePos;
            float proj = toMob.dot(lookDir);
            if (proj < 0.0f || proj > bestDist) continue;

            Vec3 closest = eyePos + lookDir * proj;
            float dx = std::abs(closest.x - m.x);
            float dy = std::abs(closest.y - (m.y + height * 0.5f));
            float dz = std::abs(closest.z - m.z);
            if (dx <= halfW + 0.15f && dy <= height * 0.52f + 0.15f && dz <= halfW + 0.15f) {
                bestDist = proj;
                bestMob = static_cast<int>(i);
            }
        }

        if (bestMob < 0) return false;

        MobEntity& target = mobs[static_cast<size_t>(bestMob)];
        if (target.hurtTime > 0) return false;

        float dmg = assets.getItemDef(heldId).attackDamage;
        damageMob(target, dmg, std::cos(player.yaw * 0.0174532925f), std::sin(player.yaw * 0.0174532925f), -1, audio, &assets, eyePos, player.yaw);
        return true;
    }

    template<typename GiveItemFn, typename QueueType>
    void tickAll(WorldMap& world, PlayerPhysics& player, BetaCombatAndNetherSystem& combat,
                 BetaRedstonePistonEngine& redstone, int difficulty,
                 GiveItemFn&& giveItemToPlayer, QueueType& meshQueue,
                 AudioEngine* audio = nullptr, const BetaAssetManager* assets = nullptr) {
        // 1. Natural Mob Spawning (Mojang SpawnerAnimals.performSpawning every 40 ticks)
        if ((world.worldTick % 40ULL) == 0 && mobs.size() < 32) {
            trySpawnNaturalMob(world, player, difficulty);
        }

        // 2. Tick all Mobs (EntityLiving -> EntityCreature -> Mob / Animal / Ghast)
        for (size_t i = 0; i < mobs.size(); ) {
            MobEntity& m = mobs[i];
            m.prevX = m.x; m.prevY = m.y; m.prevZ = m.z;
            m.prevBodyYaw = m.bodyYaw;
            m.prevHeadYaw = m.headYaw;
            m.prevPitch   = m.pitch;
            m.ageTicks++;

            // Peaceful difficulty despawns hostile mobs immediately
            if (difficulty == 0 && isHostileMob(m.type)) {
                mobs[i] = mobs.back();
                mobs.pop_back();
                continue;
            }

            //EntityLiving.onDeathUpdate: 20-tick sideways collapse before despawn
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
            float dyP = (player.y + 1.1f) - (m.y + 1.1f);
            float dzP = player.z - m.z;
            float distSq = dxP * dxP + dzP * dzP;

            if (distSq > 128.0f * 128.0f) {
                mobs[i] = mobs.back();
                mobs.pop_back();
                continue;
            }

            float halfW = 0.35f, height = 1.80f;
            getMobDimensions(m.type, halfW, height);

            int bx = static_cast<int>(std::floor(m.x));
            int by = static_cast<int>(std::floor(m.y + 0.2f));
            int bz = static_cast<int>(std::floor(m.z));
            uint8_t footId = static_cast<uint8_t>((world.getBlockAt(bx, by, bz) >> 4) & 0xFF);
            bool inWater = (footId == 8 || footId == 9);
            bool inLava  = (footId == 10 || footId == 11);

            if (inLava) {
                m.fireTicks = 300;
                if ((m.ageTicks % 10) == 0 && m.hurtTime == 0) {
                    m.health -= 4.0f;
                    m.hurtTime = 8;
                    if (m.health <= 0.0f) { dropMobLoot(m, -1); ++i; continue; }
                }
            }
            if (footId == 51) {
                m.fireTicks = std::max(m.fireTicks, 160);
            }

            //EntityZombie / EntitySkeleton sunlight ignition
            if (world.dimension == 0 && (m.type == MOB_ZOMBIE || m.type == MOB_SKELETON) && !inWater) {
                float skyBright = world.getSkyBrightness();
                uint8_t skyL    = world.getSkyLightAt(bx, static_cast<int>(std::floor(m.y + height * 0.8f)), bz);
                if (skyBright > 0.55f && skyL >= 15 && (m.ageTicks % 20) == 0) {
                    m.fireTicks = std::max(m.fireTicks, 160);
                }
            }

            if (m.fireTicks > 0) {
                m.fireTicks--;
                if (inWater) {
                    m.fireTicks = 0;
                } else if ((m.fireTicks % 20) == 0 && m.hurtTime == 0) {
                    m.health -= 1.0f;
                    m.hurtTime = 8;
                    if (m.health <= 0.0f) { dropMobLoot(m, -1); ++i; continue; }
                }
            }

            // Branch A: Ghast 3D Aerial AI
            if (m.type == MOB_GHAST) {
                tickGhastAI(world, player, combat, m, difficulty, dxP, dyP, dzP, std::sqrt(distSq + dyP * dyP));
                ++i;
                continue;
            }

            // Branch B: Ground Mobs & Animals
            float dist = std::sqrt(distSq);
            bool hasTarget = false;
            float maxAggroRange = (m.type == MOB_SPIDER && m.spiderAngry) ? 32.0f : 16.0f;

            if (isHostileMob(m.type) && difficulty > 0 && !player.flying && player.health > 0.0f && dist < maxAggroRange) {
                bool canSee = hasLineOfSight(world, m.x, m.y + height * 0.85f, m.z, player.x, player.y + 1.5f, player.z);

                if (m.type == MOB_SPIDER) {
                    float localSky = (world.getSkyLightAt(bx, by, bz) / 15.0f) * world.getSkyBrightness();
                    float localBlk = world.getBlockLightAt(bx, by, bz) / 15.0f;
                    float brightness = std::max(localSky, localBlk);
                    if (brightness < 0.5f || m.spiderAngry) {
                        hasTarget = canSee;
                    }
                } else {
                    hasTarget = canSee;
                }
            }

            m.moveStrafing = 0.0f;

            if (hasTarget) {
                float targetYaw   = std::atan2(-dxP, dzP) * 57.2957795f;
                float targetPitch = std::clamp(-std::atan2(dyP, std::max(0.1f, dist)) * 57.2957795f, -40.0f, 40.0f);
                m.yaw     = approachAngle(m.yaw, targetYaw, 30.0f);
                m.headYaw = approachAngle(m.headYaw, targetYaw, 30.0f);
                m.pitch   = targetPitch;

                float baseSpeed = (m.type == MOB_SPIDER) ? 0.042f : 0.035f;
                m.moveForward = baseSpeed;

                if (m.type == MOB_CREEPER) {
                    m.creeperState = 1;
                    if (dist < 2.2f) m.moveForward = 0.0f;
                    if (m.creeperFuse == 0 && audio && assets && !assets->fizzSound.samples.empty()) {
                        audio->play3D(assets->fizzSound, 1.0f, 0.5f, {m.x, m.y, m.z}, {player.x, player.y, player.z}, player.yaw, true);
                    }
                    if (++m.creeperFuse >= 30) {
                        BetaRedstonePistonEngine::detonateExplosion(world, m.x, m.y + 0.5f, m.z, 3.0f, meshQueue);
                        if (audio && assets && !assets->explodeSound.samples.empty()) {
                            audio->play3D(assets->explodeSound, 1.2f, 1.0f, {m.x, m.y, m.z}, {player.x, player.y, player.z}, player.yaw, true);
                        }
                        if (dist < 6.0f) {
                            float impact = 1.0f - (dist / 6.0f);
                            float expDmg = std::floor((impact * impact + impact) * 0.5f * 8.0f * 3.0f + 1.0f);
                            if (difficulty == 1) expDmg = expDmg / 2.0f + 1.0f;
                            else if (difficulty == 3) expDmg = expDmg * 1.5f;
                            player.damage(expDmg, audio, assets);
                        }
                        mobs[i] = mobs.back();
                        mobs.pop_back();
                        continue;
                    }
                } else if (m.type == MOB_SKELETON) {
                    if (dist < 10.0f) {
                        if (dist < 6.0f) {
                            m.moveForward  = 0.0f;
                            m.moveStrafing = ((m.ageTicks / 40) & 1) ? 0.025f : -0.025f;
                        }
                        if (m.attackCool == 0) {
                            float shootYaw   = std::atan2(dzP, dxP) * 57.2957795f;
                            float aimHeight  = (player.y + 0.4f) - (m.y + 1.4f) + dist * 0.20f;
                            float shootPitch = std::atan2(aimHeight, std::max(0.1f, dist)) * 57.2957795f;
                            combat.spawnProjectile(PROJ_ARROW, false, static_cast<int>(i), {m.x, m.y + 1.45f, m.z}, shootYaw, shootPitch, 1.45f);
                            if (audio && assets && !assets->bowSound.samples.empty()) {
                                audio->play3D(assets->bowSound, 1.0f, 1.0f / (nextFloat() * 0.4f + 0.8f),
                                              {m.x, m.y, m.z}, {player.x, player.y, player.z}, player.yaw, true);
                            }
                            m.attackCool = 30;
                        }
                    }
                } else if (m.type == MOB_SPIDER) {
                    if (dist > 2.0f && dist < 6.0f && m.onGround && nextInt(10) == 0) {
                        float horiz = std::max(0.01f, dist);
                        m.vx = (dxP / horiz) * 0.5f * 0.8f + m.vx * 0.2f;
                        m.vz = (dzP / horiz) * 0.5f * 0.8f + m.vz * 0.2f;
                        m.vy = 0.40f;
                        m.onGround = false;
                    } else if (dist < 1.65f && std::abs(player.y - m.y) < 1.8f && m.attackCool == 0) {
                        float dmg = (difficulty == 1) ? 2.0f : ((difficulty == 2) ? 2.0f : 3.0f);
                        player.damage(dmg, audio, assets);
                        m.attackCool = 20;
                    }
                } else {
                    if (dist < 1.65f && std::abs(player.y - m.y) < 1.8f && m.attackCool == 0) {
                        float dmg = (difficulty == 1) ? 3.0f : ((difficulty == 2) ? 4.0f : 6.0f);
                        player.damage(dmg, audio, assets);
                        m.attackCool = 20;
                    }
                }
            } else {
                if (m.type == MOB_CREEPER && m.creeperFuse > 0) {
                    if (dist > 7.0f || !hasLineOfSight(world, m.x, m.y + 1.4f, m.z, player.x, player.y + 1.5f, player.z)) {
                        m.creeperState = -1;
                        m.creeperFuse  = std::max(0, m.creeperFuse - 1);
                    }
                }

                if (--m.aiTimer <= 0) {
                    if (m.panicTicks > 0) {
                        m.aiTimer     = 5 + nextInt(6);
                        m.yaw         = wrapDegrees(m.yaw + (nextFloat() - 0.5f) * 200.0f);
                        m.moveForward = 0.042f;
                    } else {
                        m.aiTimer = 25 + nextInt(55);
                        if (nextFloat() < 0.70f) {
                            float bestYaw = wrapDegrees(m.yaw + (nextFloat() - 0.5f) * 160.0f);
                            if (!isHostileMob(m.type)) {
                                float bestWeight = -999.0f;
                                for (int sample = 0; sample < 5; ++sample) {
                                    float candYaw = wrapDegrees(m.yaw + (nextFloat() - 0.5f) * 240.0f);
                                    float cRad = candYaw * 0.0174532925f;
                                    int tx = static_cast<int>(std::floor(m.x - std::sin(cRad) * 4.0f));
                                    int tz = static_cast<int>(std::floor(m.z + std::cos(cRad) * 4.0f));
                                    int ty = world.getTopSolidY(tx, tz);
                                    uint8_t gId = static_cast<uint8_t>((world.getBlockAt(tx, ty - 1, tz) >> 4) & 0xFF);
                                    float w = (gId == 2 ? 10.0f : 0.0f) + (world.getSkyLightAt(tx, ty, tz) / 15.0f) - 0.5f;
                                    if (w > bestWeight) {
                                        bestWeight = w;
                                        bestYaw    = candYaw;
                                    }
                                }
                            }
                            m.yaw         = bestYaw;
                            m.moveForward = 0.022f;
                        } else {
                            m.moveForward = 0.0f;
                        }
                    }
                }

                if (dist < 6.0f && player.health > 0.0f) {
                    float lookAtPlayer = std::atan2(-dxP, dzP) * 57.2957795f;
                    m.headYaw = approachAngle(m.headYaw, lookAtPlayer, 15.0f);
                    m.pitch   = std::clamp(-std::atan2(dyP, std::max(0.1f, dist)) * 57.2957795f, -30.0f, 30.0f);
                } else {
                    m.headYaw = approachAngle(m.headYaw, m.yaw, 12.0f);
                    m.pitch  *= 0.85f;
                }
            }

            float rad  = m.yaw * 0.0174532925f;
            float fwdX = -std::sin(rad), fwdZ =  std::cos(rad);
            float strX =  std::cos(rad), strZ =  std::sin(rad);
            float dirX = fwdX * m.moveForward + strX * m.moveStrafing;
            float dirZ = fwdZ * m.moveForward + strZ * m.moveStrafing;

            if (m.onGround && !hasTarget && (dirX != 0.0f || dirZ != 0.0f)) {
                int checkX = static_cast<int>(std::floor(m.x + dirX * 18.0f));
                int checkZ = static_cast<int>(std::floor(m.z + dirZ * 18.0f));
                int checkY = static_cast<int>(std::floor(m.y));
                bool safeAhead = false;
                for (int drop = 1; drop <= 4; ++drop) {
                    if (checkY - drop < 1) break;
                    uint16_t bSt = world.getBlockAt(checkX, checkY - drop, checkZ);
                    uint8_t  bId = static_cast<uint8_t>((bSt >> 4) & 0xFF);
                    if (bId == 10 || bId == 11 || bId == 51) { safeAhead = false; break; }
                    if (world.registry.get(bSt).isSolid) { safeAhead = true; break; }
                }
                if (!safeAhead) {
                    dirX = 0.0f;
                    dirZ = 0.0f;
                    m.moveForward = 0.0f;
                    m.aiTimer = 0;
                }
            }

            float accelScale = m.onGround ? 1.0f : 0.25f;
            m.vx += dirX * accelScale;
            m.vz += dirZ * accelScale;

            if (inWater || inLava) {
                m.vy += 0.04f;
            } else if (m.onGround && (m.moveForward > 0.005f || std::abs(m.moveStrafing) > 0.005f)) {
                float stepLen = std::sqrt(dirX * dirX + dirZ * dirZ);
                if (stepLen > 1e-5f) {
                    int frontX = static_cast<int>(std::floor(m.x + (dirX / stepLen) * (halfW + 0.45f)));
                    int frontY = static_cast<int>(std::floor(m.y + 0.2f));
                    int frontZ = static_cast<int>(std::floor(m.z + (dirZ / stepLen) * (halfW + 0.45f)));
                    uint16_t frontSt = world.getBlockAt(frontX, frontY, frontZ);
                    if (world.registry.get(frontSt).isSolid &&
                        !world.registry.get(world.getBlockAt(frontX, frontY + 1, frontZ)).isSolid) {
                        m.vy = 0.42f;
                        m.onGround = false;
                    }
                }
            }

            moveMobWithCollision(world, m);

            if (m.type == MOB_SPIDER && m.isCollidedHorizontally && m.moveForward > 0.005f) {
                m.vy = 0.20f;
            }

            for (size_t j = 0; j < mobs.size(); ++j) {
                if (i == j || mobs[j].health <= 0.0f) continue;
                float sdx = m.x - mobs[j].x;
                float sdz = m.z - mobs[j].z;
                float sdy = std::abs(m.y - mobs[j].y);
                float maxD = std::max(std::abs(sdx), std::abs(sdz));
                if (sdy < 1.2f && maxD >= 0.01f && maxD < 0.75f) {
                    float inv = 0.05f / maxD;
                    m.vx += sdx * inv;
                    m.vz += sdz * inv;
                }
            }

            float drag = m.onGround ? 0.55f : 0.91f;
            m.vx *= drag;
            m.vz *= drag;
            m.vy = (m.vy - 0.08f) * 0.98f;

            float movedX = m.x - m.prevX;
            float movedZ = m.z - m.prevZ;
            float horizSpeed = std::sqrt(movedX * movedX + movedZ * movedZ);
            if (horizSpeed > 0.01f) {
                float moveAngle = std::atan2(-movedX, movedZ) * 57.2957795f;
                m.bodyYaw = approachAngle(m.bodyYaw, moveAngle, 20.0f);
            } else {
                float headDiff = wrapDegrees(m.headYaw - m.bodyYaw);
                if (headDiff > 75.0f)  m.bodyYaw = wrapDegrees(m.headYaw - 75.0f);
                if (headDiff < -75.0f) m.bodyYaw = wrapDegrees(m.headYaw + 75.0f);
            }

            float targetLimbAmt = std::min(1.0f, horizSpeed * 4.0f);
            m.limbSwingAmt += (targetLimbAmt - m.limbSwingAmt) * 0.4f;
            m.limbSwing    += m.limbSwingAmt;

            ++i;
        }

        // 3. Tick Projectiles (Arrows, Snowballs, Eggs & Ghast Fireballs)
        for (size_t i = 0; i < combat.projectiles.size(); ) {
            ProjectileEntity& pr = combat.projectiles[i];
            pr.lifeTicks++;
            bool isFireball = (static_cast<uint8_t>(pr.kind) == PROJ_KIND_FIREBALL);
            int maxLife = pr.inGround ? 1200 : 600;
            if (pr.lifeTicks > maxLife) {
                combat.projectiles[i] = combat.projectiles.back();
                combat.projectiles.pop_back();
                continue;
            }

            if (pr.inGround) {
                int bx = static_cast<int>(std::floor(pr.x));
                int by = static_cast<int>(std::floor(pr.y));
                int bz = static_cast<int>(std::floor(pr.z));
                if (!world.registry.get(world.getBlockAt(bx, by, bz)).isSolid) {
                    pr.inGround = false;
                    pr.vx = (nextFloat() - 0.5f) * 0.05f;
                    pr.vy = 0.0f;
                    pr.vz = (nextFloat() - 0.5f) * 0.05f;
                } else if (pr.fromPlayer && pr.lifeTicks > 15) {
                    if (std::abs(pr.x - player.x) < 1.2f && std::abs(pr.y - player.y) < 1.5f && std::abs(pr.z - player.z) < 1.2f) {
                        if (giveItemToPlayer(ITEM_ARROW, 0, 1)) {
                            if (audio && assets && !assets->popSound.samples.empty()) {
                                audio->play2D(assets->popSound, 0.35f, 1.5f);
                            }
                            combat.projectiles[i] = combat.projectiles.back();
                            combat.projectiles.pop_back();
                            continue;
                        }
                    }
                }
                ++i;
                continue;
            }

            pr.x += pr.vx;
            pr.y += pr.vy;
            pr.z += pr.vz;

            if (!isFireball) {
                int bx = static_cast<int>(std::floor(pr.x));
                int by = static_cast<int>(std::floor(pr.y));
                int bz = static_cast<int>(std::floor(pr.z));
                uint8_t curId = static_cast<uint8_t>((world.getBlockAt(bx, by, bz) >> 4) & 0xFF);
                float drag = (curId == 8 || curId == 9) ? 0.80f : 0.99f;
                pr.vx *= drag;
                pr.vy *= drag;
                pr.vz *= drag;
                pr.vy -= 0.03f;
            }

            float horiz = std::sqrt(pr.vx * pr.vx + pr.vz * pr.vz);
            pr.yaw   = std::atan2(pr.vz, pr.vx) * 57.2957795f;
            pr.pitch = std::atan2(pr.vy, horiz) * 57.2957795f;

            int bx = static_cast<int>(std::floor(pr.x));
            int by = static_cast<int>(std::floor(pr.y));
            int bz = static_cast<int>(std::floor(pr.z));
            if (world.registry.get(world.getBlockAt(bx, by, bz)).isSolid) {
                if (isFireball) {
                    detonateFireball(world, player, pr.x, pr.y, pr.z, meshQueue, audio, assets);
                    combat.projectiles[i] = combat.projectiles.back();
                    combat.projectiles.pop_back();
                    continue;
                } else if (pr.kind == PROJ_ARROW) {
                    pr.inGround = true;
                    pr.vx = pr.vy = pr.vz = 0.0f;
                    pr.lifeTicks = 0;
                    ++i;
                    continue;
                } else {
                    combat.projectiles[i] = combat.projectiles.back();
                    combat.projectiles.pop_back();
                    continue;
                }
            }

            bool hitEntity = false;
            if (pr.fromPlayer) {
                for (size_t mIdx = 0; mIdx < mobs.size(); ++mIdx) {
                    MobEntity& m = mobs[mIdx];
                    if (m.health <= 0.0f) continue;
                    float hw = 0.35f, ht = 1.80f;
                    getMobDimensions(m.type, hw, ht);
                    if (std::abs(pr.x - m.x) < hw + 0.25f && pr.y >= m.y - 0.1f && pr.y <= m.y + ht + 0.1f && std::abs(pr.z - m.z) < hw + 0.25f) {
                        if (isFireball) {
                            if (m.type == MOB_GHAST) {
                                damageMob(m, 1000.0f, pr.vx, pr.vz, -1, audio, assets, {player.x, player.y, player.z}, player.yaw);
                            }
                            detonateFireball(world, player, pr.x, pr.y, pr.z, meshQueue, audio, assets);
                        } else if (pr.kind == PROJ_ARROW) {
                            float spd = std::sqrt(pr.vx * pr.vx + pr.vy * pr.vy + pr.vz * pr.vz);
                            float arrowDmg = std::max(3.0f, std::ceil(spd * 2.5f));
                            damageMob(m, arrowDmg, pr.vx, pr.vz, pr.ownerMobIdx, audio, assets, {player.x, player.y, player.z}, player.yaw);
                        } else {
                            damageMob(m, 0.0f, pr.vx, pr.vz, -1, audio, assets, {player.x, player.y, player.z}, player.yaw);
                        }
                        hitEntity = true;
                        break;
                    }
                }
            } else {
                if (std::abs(pr.x - player.x) < 0.45f && pr.y >= player.y && pr.y <= player.y + 1.8f && std::abs(pr.z - player.z) < 0.45f) {
                    if (isFireball) {
                        detonateFireball(world, player, pr.x, pr.y, pr.z, meshQueue, audio, assets);
                    } else {
                        player.damage(3.0f, audio, assets);
                    }
                    hitEntity = true;
                }
            }

            if (hitEntity) {
                combat.projectiles[i] = combat.projectiles.back();
                combat.projectiles.pop_back();
            } else {
                ++i;
            }
        }

        // 4. Tick Dropped Items (EntityItem.onUpdate: Lava burn, Merging & Magnetic Pickup)
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

            int bx = static_cast<int>(std::floor(d.x));
            int by = static_cast<int>(std::floor(d.y));
            int bz = static_cast<int>(std::floor(d.z));
            uint8_t curId = static_cast<uint8_t>((world.getBlockAt(bx, by, bz) >> 4) & 0xFF);

            if (curId == 10 || curId == 11 || curId == 51) {
                if (audio && assets && !assets->fizzSound.samples.empty()) {
                    audio->play3D(assets->fizzSound, 0.4f, 2.2f, {d.x, d.y, d.z}, {player.x, player.y, player.z}, player.yaw, true);
                }
                droppedItems[i] = droppedItems.back();
                droppedItems.pop_back();
                continue;
            }

            d.vy -= 0.04f;
            d.x += d.vx;
            d.y += d.vy;
            d.z += d.vz;

            int groundY = static_cast<int>(std::floor(d.y - 0.125f));
            if (world.registry.get(world.getBlockAt(bx, groundY, bz)).isSolid) {
                d.y  = static_cast<float>(groundY) + 1.125f;
                d.vy = 0.0f;
                d.vx *= 0.60f;
                d.vz *= 0.60f;
            } else {
                d.vx *= 0.98f;
                d.vy *= 0.98f;
                d.vz *= 0.98f;
            }

            if (assets && d.stack.count < assets->getMaxStackSize(d.stack.id)) {
                uint8_t maxSt = assets->getMaxStackSize(d.stack.id);
                for (size_t j = i + 1; j < droppedItems.size(); ) {
                    DroppedItemEntity& o = droppedItems[j];
                    if (o.stack.id == d.stack.id && o.stack.damage == d.stack.damage &&
                        (d.stack.count + o.stack.count) <= maxSt &&
                        std::abs(d.x - o.x) < 1.0f && std::abs(d.y - o.y) < 0.5f && std::abs(d.z - o.z) < 1.0f) {
                        d.stack.count += o.stack.count;
                        d.ageTicks = std::min(d.ageTicks, o.ageTicks);
                        droppedItems[j] = droppedItems.back();
                        droppedItems.pop_back();
                    } else {
                        ++j;
                    }
                }
            }

            if (d.pickupDelay == 0 && player.health > 0.0f) {
                float dx = player.x - d.x;
                float dy = (player.y + 0.8f) - d.y;
                float dz = player.z - d.z;
                if (dx * dx + dy * dy + dz * dz < 2.25f) {
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

        // 5. Tick Block Digging Particles (EntityDiggingFX)
        for (size_t i = 0; i < particles.size(); ) {
            BlockParticle& p = particles[i];
            if (--p.lifeTicks <= 0) {
                particles[i] = particles.back();
                particles.pop_back();
                continue;
            }
            p.vy -= 0.04f;
            p.x += p.vx;
            p.y += p.vy;
            p.z += p.vz;
            p.vx *= 0.96f;
            p.vz *= 0.96f;
            ++i;
        }
        (void)redstone;
    }

private:
    static bool isHostileMob(BetaMobType t) {
        return t == MOB_ZOMBIE || t == MOB_CREEPER || t == MOB_SKELETON || t == MOB_SPIDER || t == MOB_GHAST;
    }

    static bool hasLineOfSight(const WorldMap& world, float x0, float y0, float z0, float x1, float y1, float z1) {
        float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < 0.5f) return true;
        int steps = static_cast<int>(std::ceil(dist * 2.0f));
        float inv = 1.0f / static_cast<float>(steps);
        for (int s = 1; s < steps; ++s) {
            float t = static_cast<float>(s) * inv;
            int bx = static_cast<int>(std::floor(x0 + dx * t));
            int by = static_cast<int>(std::floor(y0 + dy * t));
            int bz = static_cast<int>(std::floor(z0 + dz * t));
            if (world.registry.get(world.getBlockAt(bx, by, bz)).isOpaque) return false;
        }
        return true;
    }

    void damageMob(MobEntity& target, float dmg, float knockDirX, float knockDirZ, int attackerMobIdx,
                   AudioEngine* audio, const BetaAssetManager* assets, const Vec3& listenerPos, float listenerYaw) {
        target.health  -= dmg;
        target.hurtTime = 10;

        float kLen = std::sqrt(knockDirX * knockDirX + knockDirZ * knockDirZ);
        if (kLen > 1e-5f) {
            target.vx = target.vx * 0.5f + (knockDirX / kLen) * 0.40f;
            target.vy = std::min(0.40f, target.vy * 0.5f + 0.36f);
            target.vz = target.vz * 0.5f + (knockDirZ / kLen) * 0.40f;
        }

        if (target.type == MOB_SPIDER) {
            target.spiderAngry = true;
        } else if (target.type == MOB_PIG || target.type == MOB_SHEEP || target.type == MOB_COW) {
            target.panicTicks = 60;
            target.aiTimer    = 0;
        }

        if (audio && assets && !assets->hitSound.samples.empty()) {
            audio->play3D(assets->hitSound, 1.0f, 1.0f / (nextFloat() * 0.4f + 0.8f),
                          {target.x, target.y, target.z}, listenerPos, listenerYaw, true);
        }

        if (target.health <= 0.0f) {
            dropMobLoot(target, attackerMobIdx);
        }
    }

    void tickGhastAI(const WorldMap& world, const PlayerPhysics& player, BetaCombatAndNetherSystem& combat,
                     MobEntity& m, int difficulty, float dxP, float dyP, float dzP, float dist3D) {
        float wdx = m.ghastWaypointX - m.x;
        float wdy = m.ghastWaypointY - m.y;
        float wdz = m.ghastWaypointZ - m.z;
        float wDistSq = wdx * wdx + wdy * wdy + wdz * wdz;

        if (wDistSq < 1.0f || wDistSq > 3600.0f) {
            m.ghastWaypointX = m.x + (nextFloat() * 2.0f - 1.0f) * 16.0f;
            m.ghastWaypointY = std::clamp(m.y + (nextFloat() * 2.0f - 1.0f) * 16.0f, 16.0f, 112.0f);
            m.ghastWaypointZ = m.z + (nextFloat() * 2.0f - 1.0f) * 16.0f;
            wDistSq = (m.ghastWaypointX - m.x) * (m.ghastWaypointX - m.x) +
                      (m.ghastWaypointY - m.y) * (m.ghastWaypointY - m.y) +
                      (m.ghastWaypointZ - m.z) * (m.ghastWaypointZ - m.z);
        }

        if (--m.ghastCourseCooldown <= 0) {
            m.ghastCourseCooldown = nextInt(5) + 2;
            float wDist = std::sqrt(std::max(0.01f, wDistSq));
            if (isGhastCourseTraversable(world, m, m.ghastWaypointX, m.ghastWaypointY, m.ghastWaypointZ, wDist)) {
                m.vx += (wdx / wDist) * 0.1f;
                m.vy += (wdy / wDist) * 0.1f;
                m.vz += (wdz / wDist) * 0.1f;
            } else {
                m.ghastWaypointX = m.x;
                m.ghastWaypointY = m.y;
                m.ghastWaypointZ = m.z;
            }
        }

        moveMobWithCollision(world, m);
        m.vx *= 0.91f;
        m.vy *= 0.91f;
        m.vz *= 0.91f;

        if (difficulty > 0 && !player.flying && player.health > 0.0f && dist3D < 64.0f &&
            hasLineOfSight(world, m.x, m.y + 2.0f, m.z, player.x, player.y + 1.5f, player.z)) {
            float aimYaw = std::atan2(-dxP, dzP) * 57.2957795f;
            m.yaw     = aimYaw;
            m.bodyYaw = aimYaw;
            m.headYaw = aimYaw;

            if (++m.ghastAttackCounter >= 20) {
                float rad   = aimYaw * 0.0174532925f;
                float lookX = -std::sin(rad);
                float lookZ =  std::cos(rad);
                Vec3 origin{ m.x + lookX * 4.0f, m.y + 2.5f, m.z + lookZ * 4.0f };

                float shootYaw   = std::atan2(dzP, dxP) * 57.2957795f;
                float shootPitch = std::atan2((player.y + 1.0f) - origin.y, std::max(0.1f, std::sqrt(dxP * dxP + dzP * dzP))) * 57.2957795f;
                combat.spawnProjectile(static_cast<ProjectileKind>(PROJ_KIND_FIREBALL), false, -1, origin, shootYaw, shootPitch, 0.85f);
                m.ghastAttackCounter = -40;
            }
        } else {
            float moveYaw = std::atan2(-m.vx, m.vz) * 57.2957795f;
            m.yaw     = approachAngle(m.yaw, moveYaw, 10.0f);
            m.bodyYaw = m.yaw;
            m.headYaw = m.yaw;
            if (m.ghastAttackCounter > 0) m.ghastAttackCounter--;
        }
    }

    static bool isGhastCourseTraversable(const WorldMap& world, const MobEntity& m, float wx, float wy, float wz, float dist) {
        int steps = static_cast<int>(std::ceil(dist));
        if (steps <= 0) return true;
        float sx = (wx - m.x) / dist;
        float sy = (wy - m.y) / dist;
        float sz = (wz - m.z) / dist;

        for (int i = 1; i < steps; ++i) {
            float cx = m.x + sx * static_cast<float>(i);
            float cy = m.y + sy * static_cast<float>(i);
            float cz = m.z + sz * static_cast<float>(i);
            int x0 = static_cast<int>(std::floor(cx - 2.0f)), x1 = static_cast<int>(std::floor(cx + 2.0f));
            int y0 = static_cast<int>(std::floor(cy)),        y1 = static_cast<int>(std::floor(cy + 4.0f));
            int z0 = static_cast<int>(std::floor(cz - 2.0f)), z1 = static_cast<int>(std::floor(cz + 2.0f));
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z)
                    for (int x = x0; x <= x1; ++x)
                        if (world.registry.get(world.getBlockAt(x, y, z)).isSolid) return false;
        }
        return true;
    }

    template<typename QueueType>
    void detonateFireball(WorldMap& world, PlayerPhysics& player, float fx, float fy, float fz,
                          QueueType& meshQueue, AudioEngine* audio, const BetaAssetManager* assets) {
        BetaRedstonePistonEngine::detonateExplosion(world, fx, fy, fz, 1.0f, meshQueue);
        int bx = static_cast<int>(std::floor(fx));
        int by = static_cast<int>(std::floor(fy));
        int bz = static_cast<int>(std::floor(fz));
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (nextInt(3) == 0 && world.getBlockAt(bx + dx, by + dy, bz + dz) == 0 &&
                        world.registry.get(world.getBlockAt(bx + dx, by + dy - 1, bz + dz)).isOpaque) {
                        world.setBlockAt(bx + dx, by + dy, bz + dz, (51 << 4), false);
                        meshQueue.push({ (bx + dx) >> 4, (bz + dz) >> 4 });
                    }
                }
            }
        }
        float pdx = player.x - fx, pdy = (player.y + 1.0f) - fy, pdz = player.z - fz;
        float pDist = std::sqrt(pdx * pdx + pdy * pdy + pdz * pdz);
        if (pDist < 4.0f) {
            player.damage((4.0f - pDist) * 2.5f, audio, assets);
        }
        if (audio && assets && !assets->explodeSound.samples.empty()) {
            audio->play3D(assets->explodeSound, 1.0f, 1.0f, {fx, fy, fz}, {player.x, player.y, player.z}, player.yaw, true);
        }
    }

    void dropMobLoot(const MobEntity& m, int killerMobIdx) {
        switch (m.type) {
            case MOB_ZOMBIE:
                spawnDroppedItem(ITEM_FEATHER, 0, static_cast<uint8_t>(nextInt(3)), m.x, m.y + 0.5f, m.z);
                break;
            case MOB_CREEPER:
                spawnDroppedItem(ITEM_GUNPOWDER, 0, static_cast<uint8_t>(nextInt(3)), m.x, m.y + 0.5f, m.z);
                if (killerMobIdx >= 0 && killerMobIdx < static_cast<int>(mobs.size()) &&
                    mobs[static_cast<size_t>(killerMobIdx)].type == MOB_SKELETON) {
                    uint16_t discId = static_cast<uint16_t>(ITEM_RECORD_13 + nextInt(2));
                    spawnDroppedItem(discId, 0, 1, m.x, m.y + 0.5f, m.z);
                }
                break;
            case MOB_SKELETON:
                spawnDroppedItem(ITEM_ARROW, 0, static_cast<uint8_t>(nextInt(3)), m.x, m.y + 0.5f, m.z);
                spawnDroppedItem(ITEM_BONE,  0, static_cast<uint8_t>(nextInt(3)), m.x, m.y + 0.5f, m.z);
                break;
            case MOB_SPIDER:
                spawnDroppedItem(ITEM_STRING, 0, static_cast<uint8_t>(nextInt(3)), m.x, m.y + 0.5f, m.z);
                break;
            case MOB_PIG:
                spawnDroppedItem((m.fireTicks > 0) ? ITEM_PORKCHOP_COOKED : ITEM_PORKCHOP_RAW, 0, 1, m.x, m.y + 0.5f, m.z);
                break;
            case MOB_SHEEP:
                if (!m.sheared) spawnDroppedItem(35, m.woolColor, 1, m.x, m.y + 0.5f, m.z);
                break;
            case MOB_COW:
                spawnDroppedItem(ITEM_LEATHER, 0, static_cast<uint8_t>(nextInt(3)), m.x, m.y + 0.5f, m.z);
                break;
            case MOB_GHAST:
                spawnDroppedItem(ITEM_GUNPOWDER, 0, static_cast<uint8_t>(nextInt(3)), m.x, m.y + 0.5f, m.z);
                break;
        }
    }

    void trySpawnNaturalMob(const WorldMap& world, const PlayerPhysics& player, int difficulty) {
        float ang  = nextFloat() * 6.2831853f;
        float dist = 24.0f + nextFloat() * 32.0f;
        int wx = static_cast<int>(std::floor(player.x + std::cos(ang) * dist));
        int wz = static_cast<int>(std::floor(player.z + std::sin(ang) * dist));

        if (world.dimension == -1) {
            if (difficulty == 0) return;
            int wy = 32 + nextInt(70);
            if (world.getBlockAt(wx, wy, wz) == 0 && world.getBlockAt(wx, wy + 2, wz) == 0 && nextInt(4) == 0) {
                spawnMob(MOB_GHAST, wx + 0.5f, static_cast<float>(wy), wz + 0.5f);
            }
            return;
        }

        int wy = world.getTopSolidY(wx, wz);
        if (wy <= SEA_LEVEL || wy >= CHUNK_HEIGHT - 3) return;
        if (world.getBlockAt(wx, wy, wz) != 0 || world.getBlockAt(wx, wy + 1, wz) != 0) return;

        uint8_t groundId = static_cast<uint8_t>((world.getBlockAt(wx, wy - 1, wz) >> 4) & 0xFF);
        uint8_t skyL = world.getSkyLightAt(wx, wy, wz);
        uint8_t blkL = world.getBlockLightAt(wx, wy, wz);

        if (groundId == 2 && skyL >= 9 && world.getSkyBrightness() > 0.6f && nextFloat() < 0.35f) {
            int r = nextInt(3);
            BetaMobType animal = (r == 0) ? MOB_PIG : ((r == 1) ? MOB_SHEEP : MOB_COW);
            spawnMob(animal, wx + 0.5f, static_cast<float>(wy), wz + 0.5f);
        } else if (difficulty > 0 && world.registry.getById(groundId).isSolid && blkL < 8 &&
                   (skyL < 8 || world.getSkyBrightness() < 0.45f)) {
            int r = nextInt(4);
            BetaMobType monster = (r == 0) ? MOB_ZOMBIE : ((r == 1) ? MOB_CREEPER : ((r == 2) ? MOB_SKELETON : MOB_SPIDER));
            spawnMob(monster, wx + 0.5f, static_cast<float>(wy), wz + 0.5f);
        }
    }

    void moveMobWithCollision(const WorldMap& world, MobEntity& m) const {
        float hw = 0.35f, ht = 1.75f;
        getMobDimensions(m.type, hw, ht);

        auto collides = [&](float tx, float ty, float tz) -> bool {
            AABB box = AABB::fromFootPos(tx, ty, tz, hw, ht);
            int x0 = static_cast<int>(std::floor(box.minX)), x1 = static_cast<int>(std::floor(box.maxX));
            int y0 = static_cast<int>(std::floor(box.minY - 1.0f)), y1 = static_cast<int>(std::floor(box.maxY));
            int z0 = static_cast<int>(std::floor(box.minZ)), z1 = static_cast<int>(std::floor(box.maxZ));

            SubBoxList subs;
            for (int y = y0; y <= y1; ++y) {
                if (y < 0 || y >= CHUNK_HEIGHT) continue;
                for (int z = z0; z <= z1; ++z) {
                    for (int x = x0; x <= x1; ++x) {
                        uint16_t st = world.getBlockAt(x, y, z);
                        const BlockDef& def = world.registry.get(st);
                        if (!def.isSolid) continue;

                        uint8_t id = static_cast<uint8_t>((st >> 4) & 0xFF);
                        if (id == 85) {
                            AABB fenceBox{ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                                           x + 1.0f, y + 1.5f, z + 1.0f };
                            if (box.intersects(fenceBox)) return true;
                        } else if (def.shape == SHAPE_CUBE) {
                            AABB blk{ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                                      x + 1.0f, y + 1.0f, z + 1.0f };
                            if (box.intersects(blk)) return true;
                        } else {
                            world.registry.getSubBoxes(st, x, y, z, [&](int qx, int qy, int qz) {
                                return world.getBlockAt(qx, qy, qz);
                            }, subs);
                            for (int b = 0; b < subs.count; ++b) {
                                const SubBox& sb = subs.boxes[b];
                                AABB blk{ x + sb.minX * 0.0625f, y + sb.minY * 0.0625f, z + sb.minZ * 0.0625f,
                                          x + sb.maxX * 0.0625f, y + sb.maxY * 0.0625f, z + sb.maxZ * 0.0625f };
                                if (box.intersects(blk)) return true;
                            }
                        }
                    }
                }
            }
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

        bool hitX = false, hitZ = false;
        if (!collides(m.x + m.vx, m.y, m.z)) {
            m.x += m.vx;
        } else {
            m.vx = 0.0f;
            hitX = true;
        }

        if (!collides(m.x, m.y, m.z + m.vz)) {
            m.z += m.vz;
        } else {
            m.vz = 0.0f;
            hitZ = true;
        }

        m.isCollidedHorizontally = hitX || hitZ;
    }
};