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
static constexpr float SLASH_PHASE_DURATION = 0.4f;

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
static constexpr float ZOOM_IDLE = 1.3f;
static constexpr float ZOOM_JUMP = 1.15f;
static constexpr float SCREEN_CENTER_X = 0.5f;
static constexpr float SCREEN_CENTER_Y = 0.45f;

// --- Easing bezier control points for jump ---
static const Vec2 JUMP_EASE_P0(0.0f, 0.0f);
static const Vec2 JUMP_EASE_P1(0.42f, 0.0f);
static const Vec2 JUMP_EASE_P2(0.58f, 1.0f);
static const Vec2 JUMP_EASE_P3(1.0f, 1.0f);

// --- Constructors ---

Player::Player()
    : jumping(false)
    , jumpTimer(0)
    , jumpDuration(0.25f)
    , swordOffset(0)
    , hasSlashed(false)
{}

void Player::reset() {
    pos = Vec2(0, 0);
    angle = 0;
    active = true;
    jumping = false;
    jumpTimer = 0;
    jumpDuration = 0.25f;
    swordOffset = 0;
    hasSlashed = false;
    swordRibbons.clear();
}

Enemy::Enemy()
    : vel(0, 0)
    , baseSpeed(0)
    , radius(0)
    , alive(true)
    , flashTimer(0)
    , blowAwayTimer(0)
    , blowAwayVel(0, 0)
    , beingBlown(false)
{}

void Enemy::reset() {
    pos = Vec2(0, 0);
    angle = 0;
    active = true;
    vel = Vec2(0, 0);
    baseSpeed = 0;
    radius = 0;
    alive = true;
    flashTimer = 0;
    blowAwayTimer = 0;
    blowAwayVel = Vec2(0, 0);
    beingBlown = false;
}

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

static Vec2 bezier(float t, Vec2 p0, Vec2 p1, Vec2 p2) {
    float u = 1.0f - t;
    return p0 * (u * u) + p1 * (2.0f * u * t) + p2 * (t * t);
}

void initGame(GameState& game, int screenW, int screenH) {
    srand(42);
    game.player.reset();
    game.player.jumpDuration = JUMP_DURATION;
    game.camera.pos = Vec2(0, 0);
    game.camera.zoom = ZOOM_IDLE;
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
        game.player.jumpTarget = enemyPos - dir * (SWORD_LENGTH * 0.5f);
        Vec2 mid = (game.player.jumpStart + game.player.jumpTarget) * 0.5f;
        Vec2 perp(-dir.y, dir.x);
        float curveAmount = CURVE_MIN + randf() * CURVE_VAR;
        if (randf() > 0.5f) curveAmount = -curveAmount;
        game.player.jumpControl = mid + perp * curveAmount;
    } else {
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
        for (int i = 0; i < needed; i++) spawnEnemy(game, gradient, 0.0f);
    }

    game.spawnTimer -= dt;
    if (aliveCount < game.maxEnemies && game.spawnTimer <= 0) {
        spawnEnemy(game, gradient, gradient * SPAWN_GRADIENT_SPEED_BOOST);
        game.spawnTimer = SPAWN_TIMER_MIN + randf() * SPAWN_TIMER_VAR;
    }

    // Jump logic (game physics)
    if (game.player.jumping) {
        game.player.jumpTimer -= dt;
        float t = 1.0f - (game.player.jumpTimer / game.player.jumpDuration);
        t = easeCubic(t);
        game.player.pos = bezier(t, game.player.jumpStart, game.player.jumpControl, game.player.jumpTarget);
    }

    // Combat: slash detection
    if (game.player.jumping && game.player.hasSlashed) {
        float swordAngle = game.player.angle + game.player.swordOffset;
        Vec2 swordTip(
            game.player.pos.x + cosf(swordAngle) * SWORD_LENGTH,
            game.player.pos.y + sinf(swordAngle) * SWORD_LENGTH
        );
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

    // Jump completion
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

    // Enemy AI movement
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

    // Remove dead enemies
    game.enemies.erase(
        std::remove_if(game.enemies.begin(), game.enemies.end(),
            [](const Enemy& e) { return !e.alive && e.flashTimer <= 0; }),
        game.enemies.end()
    );
}

Vec2 getWorldToScreen(const GameState& game, const Vec2& worldPos, int screenW, int screenH) {
    float zoom = game.camera.zoom;
    float sx = (worldPos.x - game.camera.pos.x) * zoom + screenW * SCREEN_CENTER_X;
    float sy = (worldPos.y - game.camera.pos.y) * zoom + screenH * SCREEN_CENTER_Y;
    return Vec2(sx, sy);
}
