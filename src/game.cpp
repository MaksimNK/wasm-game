#include "game.hpp"
#include <cstdlib>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Random helpers ---
static float randf() { return (float)rand() / (float)RAND_MAX; }
static float randf(float min, float max) { return min + randf() * (max - min); }

// --- Time constants ---
static constexpr float TIME_SCALE_MIN = 0.15f;
static constexpr float TIME_SCALE_RANGE = 0.85f;

// --- Enemy count constants ---
static constexpr float BASE_ENEMY_COUNT = 3.0f;
static constexpr float ENEMY_COUNT_GRADIENT_SCALE = 2.5f;

// --- Spawn constants ---
static constexpr float SPAWN_DIST_LOW = 400.0f;
static constexpr float SPAWN_DIST_HIGH = 500.0f;
static constexpr float SPAWN_DIST_VARIANCE = 300.0f;
static constexpr float SPAWN_GRADIENT_THRESHOLD = 0.6f;
static constexpr float MIN_SPAWN_SPEED = 1.5f;
static constexpr float SPAWN_SPEED_VAR = 1.0f;
static constexpr float SPAWN_GRADIENT_SPEED_BOOST = 0.5f;
static constexpr float MIN_ENEMY_RADIUS = 14.0f;
static constexpr float ENEMY_RADIUS_VAR = 10.0f;
static constexpr float SPAWN_TIMER_MIN = 0.8f;
static constexpr float SPAWN_TIMER_VAR = 1.5f;
static constexpr int MAX_ENEMIES = 5;

// --- Attack constants ---
static constexpr float MIN_ATTACK_DIST = 40.0f;
static constexpr float JUMP_DURATION = 0.25f;
static constexpr float CURVE_MIN = 60.0f;
static constexpr float CURVE_VAR = 80.0f;
static constexpr float SLASH_TRIGGER_DIST = 70.0f;

// --- Blow away constants ---
static constexpr float BLOW_RADIUS = 350.0f;
static constexpr float BLOW_BASE_FORCE = 300.0f;
static constexpr float BLOW_MAX_ADDITIONAL_FORCE = 900.0f;
static constexpr float BLOW_DURATION = 0.7f;

// --- Sword constants ---
static constexpr float SWORD_LENGTH = 115.0f;
static constexpr float SWORD_HIT_EXTRA_RADIUS = 45.0f;
static constexpr float SWORD_WINDUP_ANGLE = -M_PI * 0.89f;
static constexpr float SWORD_SLASH_END_ANGLE = M_PI * 0.67f;
static constexpr float SWORD_WINDUP_SPEED = 8.0f;
static constexpr float SWORD_SLASH_SPEED = 25.0f;
static constexpr float SWORD_IDLE_FREQ = 2.5f;
static constexpr float SWORD_IDLE_AMP = 0.3f;
static constexpr float SWORD_IDLE_SPEED = 6.0f;
static constexpr float SLASH_PHASE_DURATION = 0.4f;

// --- Ribbon trail constants ---
static constexpr float RIBBON_LIFETIME = 0.5f;
static constexpr int RIBBON_SEGMENTS_IDLE = 2;
static constexpr int RIBBON_SEGMENTS_JUMP = 8;

// --- Enemy flash/knockout ---
static constexpr float FLASH_DURATION = 0.15f;

// --- Enemy movement constants ---
static constexpr float ENEMY_ACCEL = 0.04f;
static constexpr float ENEMY_SPEED_SCALE = 1.5f;
static constexpr float ENEMY_RHYTHM_MIN = 0.5f;
static constexpr float ENEMY_RHYTHM_MAX = 1.2f;
static constexpr float BLOW_FRICTION = 2.0f;
static constexpr float ENEMY_SPEED_PX_PER_SEC = 60.0f;

// --- Camera constants ---
static constexpr float CAMERA_FOLLOW_SPEED = 5.0f;
static constexpr float CAMERA_ZOOM_SPEED = 4.0f;
static constexpr float ZOOM_IDLE = 1.3f;
static constexpr float ZOOM_JUMP = 1.15f;
static constexpr float SCREEN_CENTER_X = 0.5f;
static constexpr float SCREEN_CENTER_Y = 0.45f;

// --- Player rotation ---
static constexpr float ANGLE_ROTATION_SPEED = 12.0f;

// --- Easing bezier control points for jump ---
static const Vec2 JUMP_EASE_P0(0.0f, 0.0f);
static const Vec2 JUMP_EASE_P1(0.42f, 0.0f);
static const Vec2 JUMP_EASE_P2(0.58f, 1.0f);
static const Vec2 JUMP_EASE_P3(1.0f, 1.0f);

float getBrightnessAtTime(const Timeline& timeline, float time) {
    if (timeline.gradient.empty()) return 0.0f;
    int idx = (int)(time * timeline.fps);
    if (idx < 0) return timeline.gradient[0];
    if (idx >= (int)timeline.gradient.size()) return timeline.gradient.back();
    return timeline.gradient[idx];
}

float getTimeScale(float gradient) {
    return TIME_SCALE_MIN + TIME_SCALE_RANGE * gradient;
}

// Cubic bezier: 4 control points for smoother easing
static Vec2 cubicBezier(float t, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3) {
    float u = 1.0f - t;
    float uu = u * u;
    float tt = t * t;
    return p0 * (uu * u) + p1 * (3.0f * uu * t) + p2 * (3.0f * u * tt) + p3 * (tt * t);
}

// 1D cubic bezier easing (extracts Y from X=t)
static float easeCubic(float t) {
    return cubicBezier(t, JUMP_EASE_P0, JUMP_EASE_P1, JUMP_EASE_P2, JUMP_EASE_P3).y;
}

void initGame(GameState& game, int screenW, int screenH) {
    srand(42);

    // Player
    game.player.pos = Vec2(0, 0);
    game.player.angle = 0;
    game.player.jumping = false;
    game.player.jumpTimer = 0;
    game.player.jumpDuration = JUMP_DURATION;
    game.player.swordOffset = 0;
    game.player.hasSlashed = false;
    game.player.swordRibbons.clear();

    // Camera
    game.camera.pos = Vec2(0, 0);
    game.camera.zoom = ZOOM_IDLE;

    // Game state
    game.running = true;
    game.spawnTimer = 0;
    game.timeScale = 1.0f;
    game.gameTime = 0;
    game.musicTime = 0;
    game.enemies.clear();
    game.maxEnemies = MAX_ENEMIES;
    game.targetEnemy = -1;
}

static int findNearestEnemy(const GameState& game) {
    int nearest = -1;
    float minDist = 999999.0f;
    for (size_t i = 0; i < game.enemies.size(); i++) {
        if (!game.enemies[i].alive) continue;
        float d = (game.enemies[i].pos - game.player.pos).len();
        if (d < minDist && d > MIN_ATTACK_DIST) {
            minDist = d;
            nearest = i;
        }
    }
    return nearest;
}

static Vec2 bezier(float t, Vec2 p0, Vec2 p1, Vec2 p2) {
    float u = 1.0f - t;
    return p0 * (u * u) + p1 * (2.0f * u * t) + p2 * (t * t);
}

static float angleDiff(float a, float b) {
    float diff = a - b;
    while (diff > M_PI) diff -= 2.0f * M_PI;
    while (diff < -M_PI) diff += 2.0f * M_PI;
    return diff;
}

static void blowAwayEnemies(GameState& game, Vec2 explosionPos) {
    for (auto& e : game.enemies) {
        if (!e.alive) continue;
        Vec2 diff = e.pos - explosionPos;
        float dist = diff.len();
        if (dist < BLOW_RADIUS && dist > 0.001f) {
            Vec2 dir = diff.normalized();
            float force = (BLOW_RADIUS - dist) / BLOW_RADIUS * BLOW_MAX_ADDITIONAL_FORCE + BLOW_BASE_FORCE;
            e.blowAwayVel = dir * force;
            e.blowAwayTimer = BLOW_DURATION;
            e.beingBlown = true;
        }
    }
}

static void spawnEnemy(GameState& game, float gradient, float extraSpeed) {
    // Calculate camera view bounds
    float viewW = 800.0f / game.camera.zoom;
    float viewH = 600.0f / game.camera.zoom;
    float margin = 100.0f;
    float minSpawnDist = fmaxf(viewW, viewH) * 0.5f + margin;

    Enemy e;
    float angle = randf() * 2.0f * M_PI;
    float baseDist = gradient > SPAWN_GRADIENT_THRESHOLD ? SPAWN_DIST_HIGH : SPAWN_DIST_LOW;
    float spawnDist = fmaxf(baseDist + randf() * SPAWN_DIST_VARIANCE, minSpawnDist);
    e.pos = Vec2(game.player.pos.x + cosf(angle) * spawnDist,
                game.player.pos.y + sinf(angle) * spawnDist);

    Vec2 toPlayer = (game.player.pos - e.pos).normalized();
    float speed = MIN_SPAWN_SPEED + randf() * SPAWN_SPEED_VAR + extraSpeed;
    e.baseSpeed = speed;
    e.vel = toPlayer * speed;
    e.radius = MIN_ENEMY_RADIUS + randf() * ENEMY_RADIUS_VAR;
    e.alive = true;
    e.flashTimer = 0;
    e.blowAwayTimer = 0;
    e.blowAwayVel = Vec2(0, 0);
    e.beingBlown = false;
    game.enemies.push_back(e);
}

void processAttack(GameState& game, const Timeline& timeline) {
    if (game.player.jumping) return;

    // Init player attack state first
    game.player.jumping = true;
    game.player.hasSlashed = false;
    game.player.jumpTimer = game.player.jumpDuration;

    int target = findNearestEnemy(game);

    if (target >= 0) {
        Vec2 enemyPos = game.enemies[target].pos;
        game.targetEnemy = target;
        game.player.angle = atan2f(enemyPos.y - game.player.pos.y, enemyPos.x - game.player.pos.x);

        Vec2 dir = (enemyPos - game.player.pos).normalized();

        game.player.jumpStart = game.player.pos;
        game.player.jumpTarget = enemyPos;

        Vec2 mid = (game.player.jumpStart + game.player.jumpTarget) * 0.5f;
        Vec2 perp(-dir.y, dir.x);
        float curveAmount = CURVE_MIN + randf() * CURVE_VAR;
        if (randf() > 0.5f) curveAmount = -curveAmount;
        game.player.jumpControl = mid + perp * curveAmount;
    } else {
        // No enemy: small lunge forward
        game.targetEnemy = -1;
        Vec2 dir(cosf(game.player.angle), sinf(game.player.angle));
        game.player.jumpStart = game.player.pos;
        game.player.jumpTarget = game.player.pos + dir * 120.0f;
        game.player.jumpControl = game.player.pos + dir * 60.0f;
    }
}

void updateGame(GameState& game, const Timeline& timeline, float realDt, float musicTime) {
    if (!game.running) return;

    game.musicTime = musicTime;
    float gradient = getBrightnessAtTime(timeline, musicTime);
    game.timeScale = getTimeScale(gradient);
    float dt = realDt * game.timeScale;
    game.gameTime += dt;

    int aliveCount = 0;
    for (const auto& e : game.enemies) if (e.alive) aliveCount++;

    int targetCount = (int)(BASE_ENEMY_COUNT + gradient * ENEMY_COUNT_GRADIENT_SCALE);

    if (aliveCount < targetCount) {
        int needed = targetCount - aliveCount;
        for (int i = 0; i < needed; i++) {
            spawnEnemy(game, gradient, 0.0f);
        }
    }

    game.spawnTimer -= dt;
    if (aliveCount < game.maxEnemies && game.spawnTimer <= 0) {
        spawnEnemy(game, gradient, gradient * SPAWN_GRADIENT_SPEED_BOOST);
        game.spawnTimer = SPAWN_TIMER_MIN + randf() * SPAWN_TIMER_VAR;
    }

    // Sword animation: always on back (opposite to movement)
    float targetSwordOffset;
    if (game.player.jumping) {
        float distToTarget = (game.player.jumpTarget - game.player.pos).len();
        float startDist = (game.player.jumpTarget - game.player.jumpStart).len();
        float progress = 1.0f - distToTarget / (startDist + 0.001f);
        progress = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);

        if (!game.player.hasSlashed) {
            // Windup: sword draws back
            float t = progress * progress * (3.0f - 2.0f * progress);
            targetSwordOffset = SWORD_WINDUP_ANGLE * t;
            if (distToTarget < SLASH_TRIGGER_DIST) game.player.hasSlashed = true;
        } else {
            // Slash: sword swings through
            float slashProgress = (game.player.jumpDuration - game.player.jumpTimer) / (game.player.jumpDuration * SLASH_PHASE_DURATION);
            slashProgress = slashProgress < 0.0f ? 0.0f : (slashProgress > 1.0f ? 1.0f : slashProgress);
            float t = slashProgress * slashProgress;
            targetSwordOffset = SWORD_WINDUP_ANGLE + (SWORD_SLASH_END_ANGLE - SWORD_WINDUP_ANGLE) * t;
        }
    } else {
        // Idle: gentle sway on back
        targetSwordOffset = sinf(game.gameTime * SWORD_IDLE_FREQ) * SWORD_IDLE_AMP;
    }

    float diff = angleDiff(targetSwordOffset, game.player.swordOffset);
    float speed = game.player.jumping ? (game.player.hasSlashed ? SWORD_SLASH_SPEED : SWORD_WINDUP_SPEED) : SWORD_IDLE_SPEED;
    game.player.swordOffset += diff * dt * speed;

    // Update jump with cubic bezier easing
    if (game.player.jumping) {
        game.player.jumpTimer -= dt;

        float t = 1.0f - (game.player.jumpTimer / game.player.jumpDuration);
        t = easeCubic(t);

        game.player.pos = bezier(t, game.player.jumpStart, game.player.jumpControl, game.player.jumpTarget);
    }

    // Animate player angle toward movement direction
    Vec2 moveDir = game.player.pos - game.player.jumpStart;
    if (game.player.jumping && moveDir.len() > 0.01f) {
        float targetAngle = atan2f(moveDir.y, moveDir.x);
        float angleDiffVal = angleDiff(targetAngle, game.player.angle);
        game.player.angle += angleDiffVal * dt * ANGLE_ROTATION_SPEED;
    }

    // Calculate sword base and tip positions for ribbon trail
    float swordAngle = game.player.angle + game.player.swordOffset;
    Vec2 swordBase = game.player.pos;
    Vec2 swordTip(
        game.player.pos.x + cosf(swordAngle) * SWORD_LENGTH,
        game.player.pos.y + sinf(swordAngle) * SWORD_LENGTH
    );

    // Add ribbon trail
    int segments = game.player.jumping ? RIBBON_SEGMENTS_JUMP : RIBBON_SEGMENTS_IDLE;
    for (int i = 0; i < segments; i++) {
        SwordRibbon ribbon;
        ribbon.base = swordBase;
        ribbon.tip = swordTip;
        ribbon.lifetime = RIBBON_LIFETIME;
        ribbon.maxLifetime = RIBBON_LIFETIME;
        ribbon.gradient = gradient;
        game.player.swordRibbons.push_back(ribbon);
    }

    // Kill during slash
    if (game.player.jumping && game.player.hasSlashed) {
        for (auto& e : game.enemies) {
            if (!e.alive) continue;
            float d = (e.pos - swordTip).len();
            if (d < e.radius + SWORD_HIT_EXTRA_RADIUS) {
                blowAwayEnemies(game, e.pos);
                e.alive = false;
                e.flashTimer = FLASH_DURATION;
            }
        }
    }

    if (game.player.jumping && game.player.jumpTimer <= 0) {
        game.player.jumping = false;
        game.player.pos = game.player.jumpTarget;

        if (game.targetEnemy >= 0 && game.targetEnemy < (int)game.enemies.size() &&
            game.enemies[game.targetEnemy].alive) {
            blowAwayEnemies(game, game.enemies[game.targetEnemy].pos);
            game.enemies[game.targetEnemy].alive = false;
            game.enemies[game.targetEnemy].flashTimer = FLASH_DURATION;
        }
        game.targetEnemy = -1;
        game.player.hasSlashed = false;
    }

    // Update enemies
    for (auto& e : game.enemies) {
        if (!e.alive) continue;

        if (e.beingBlown && e.blowAwayTimer > 0) {
            e.blowAwayTimer -= dt;
            e.pos = e.pos + e.blowAwayVel * dt;
            e.blowAwayVel = e.blowAwayVel * (1.0f - dt * BLOW_FRICTION);
            if (e.blowAwayTimer <= 0) {
                e.beingBlown = false;
                e.blowAwayVel = Vec2(0, 0);
            }
        } else {
            float rhythmSpeed = e.baseSpeed * (ENEMY_RHYTHM_MIN + gradient * ENEMY_RHYTHM_MAX);
            Vec2 toPlayer = (game.player.pos - e.pos).normalized();
            e.vel.x += toPlayer.x * ENEMY_ACCEL * rhythmSpeed;
            e.vel.y += toPlayer.y * ENEMY_ACCEL * rhythmSpeed;
            float speed = e.vel.len();
            if (speed > rhythmSpeed * ENEMY_SPEED_SCALE) e.vel = e.vel.normalized() * rhythmSpeed * ENEMY_SPEED_SCALE;
            e.pos = e.pos + e.vel * dt * ENEMY_SPEED_PX_PER_SEC;
        }
        if (e.flashTimer > 0) e.flashTimer -= dt;
    }

    // Remove dead enemies after flash/knockout animation
    game.enemies.erase(
        std::remove_if(game.enemies.begin(), game.enemies.end(),
            [](const Enemy& e) { return !e.alive && e.flashTimer <= 0; }),
        game.enemies.end()
    );

    // Fade sword ribbons
    for (auto& sr : game.player.swordRibbons) sr.lifetime -= dt;
    game.player.swordRibbons.erase(
        std::remove_if(game.player.swordRibbons.begin(), game.player.swordRibbons.end(),
            [](const SwordRibbon& r) { return r.lifetime <= 0; }),
        game.player.swordRibbons.end()
    );

    // Camera
    Vec2 diffCam = game.player.pos - game.camera.pos;
    game.camera.pos = game.camera.pos + diffCam * dt * CAMERA_FOLLOW_SPEED;
    float targetZoom = game.player.jumping ? ZOOM_JUMP : ZOOM_IDLE;
    game.camera.zoom += (targetZoom - game.camera.zoom) * dt * CAMERA_ZOOM_SPEED;
}

Vec2 getWorldToScreen(const GameState& game, const Vec2& worldPos, int screenW, int screenH) {
    float zoom = game.camera.zoom;
    float sx = (worldPos.x - game.camera.pos.x) * zoom + screenW * SCREEN_CENTER_X;
    float sy = (worldPos.y - game.camera.pos.y) * zoom + screenH * SCREEN_CENTER_Y;
    return Vec2(sx, sy);
}
