#include "game.hpp"
#include <cstdlib>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float randf() { return (float)rand() / (float)RAND_MAX; }
static float randf(float min, float max) { return min + randf() * (max - min); }

float getBrightnessAtTime(const Timeline& timeline, float time) {
    if (timeline.brightness.empty()) return 0.0f;
    int idx = (int)(time * timeline.fps);
    if (idx < 0) return timeline.brightness[0];
    if (idx >= (int)timeline.brightness.size()) return timeline.brightness.back();
    return timeline.brightness[idx];
}

float getTimeScale(float brightness) {
    return 0.15f + 0.85f * brightness;
}

float getBarOpacity(float brightness) {
    if (brightness < 0.3f) {
        return brightness / 0.1f * 15.0f;
    }
    return 15.0f + (brightness - 0.3f) / 0.7f * 240.0f;
}

void initGame(GameState& game, int screenW, int screenH) {
    srand(42);
    game.playerPos = Vec2(0, 0);
    game.playerVel = Vec2(0, 0);
    game.playerAngle = 0;
    game.camera = Vec2(0, 0);
    game.cameraZoom = 1.3f;
    game.score = 0;
    game.combo = 0;
    game.jumping = false;
    game.jumpTimer = 0;
    game.jumpDuration = 0.25f;
    game.screenShake = 0;
    game.lastAttackTime = -10.0f;
    game.attackCooldown = 0.25f;
    game.running = true;
    game.spawnTimer = 0;
    game.timeScale = 1.0f;
    game.gameTime = 0;
    game.musicTime = 0;
    game.enemies.clear();
    game.swordRibbons.clear();
    game.swordAngle = 0;
    game.swordOffset = 0;
    game.swordActive = false;
    game.hasSlashed = false;
    game.maxEnemies = 5;
    game.targetEnemy = -1;
    
    game.walls.clear();
    for (int i = 0; i < 15; i++) {
        Wall w;
        w.x = randf(-600, 600);
        w.y = randf(-500, 500);
        w.w = randf(30, 100);
        w.h = randf(10, 40);
        game.walls.push_back(w);
    }
}

static int findNearestEnemy(const GameState& game) {
    int nearest = -1;
    float minDist = 999999.0f;
    for (size_t i = 0; i < game.enemies.size(); i++) {
        if (!game.enemies[i].alive) continue;
        float d = (game.enemies[i].pos - game.playerPos).len();
        if (d < minDist && d > 40.0f) {
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
        if (dist < 350.0f && dist > 0.001f) {
            Vec2 dir = diff.normalized();
            float force = (350.0f - dist) / 350.0f * 900.0f + 300.0f;
            e.blowAwayVel = dir * force;
            e.blowAwayTimer = 0.7f;
            e.beingBlown = true;
        }
    }
}

void processAttack(GameState& game, const Timeline& timeline) {
    if (game.musicTime - game.lastAttackTime < game.attackCooldown) return;
    if (game.jumping) return;
    
    game.lastAttackTime = game.musicTime;
    int target = findNearestEnemy(game);
    
    if (target >= 0) {
        Vec2 enemyPos = game.enemies[target].pos;
        game.targetEnemy = target;
        game.playerAngle = atan2f(enemyPos.y - game.playerPos.y, enemyPos.x - game.playerPos.x);
        
        Vec2 dir = (enemyPos - game.playerPos).normalized();
        float dist = (enemyPos - game.playerPos).len();
        
        game.jumping = true;
        game.jumpStart = game.playerPos;
        game.jumpTarget = enemyPos;
        game.hasSlashed = false;
        
        Vec2 mid = (game.jumpStart + game.jumpTarget) * 0.5f;
        Vec2 perp(-dir.y, dir.x);
        float curveAmount = 60.0f + randf() * 80.0f;
        if (randf() > 0.5f) curveAmount = -curveAmount;
        game.jumpControl = mid + perp * curveAmount;
        
        game.jumpTimer = game.jumpDuration;
        game.swordActive = true;
        
        float brightness = getBrightnessAtTime(timeline, game.musicTime);
        bool onBeat = brightness > 0.2f;
        
        if (onBeat) {
            game.score += 50 + game.combo * 25;
            game.combo++;
            game.screenShake = 0.3f;
        } else {
            game.combo = 0;
            game.score -= 15;
            if (game.score < 0) game.score = 0;
            game.screenShake = 0.2f;
        }
    }
}

void updateGame(GameState& game, const Timeline& timeline, float realDt, float musicTime) {
    if (!game.running) return;
    
    game.musicTime = musicTime;
    float brightness = getBrightnessAtTime(timeline, musicTime);
    game.timeScale = getTimeScale(brightness);
    float dt = realDt * game.timeScale;
    game.gameTime += dt;
    
    int aliveCount = 0;
    for (const auto& e : game.enemies) if (e.alive) aliveCount++;
    
    int targetCount = 3;
    if (brightness > 0.5f) targetCount = 4;
    if (brightness > 0.8f) targetCount = 5;
    
    if (aliveCount < targetCount) {
        int needed = targetCount - aliveCount;
        for (int i = 0; i < needed; i++) {
            Enemy e;
            float angle = randf() * 2.0f * M_PI;
            float baseDist = brightness > 0.6f ? 500.0f : 400.0f;
            float spawnDist = baseDist + randf() * 300.0f;
            e.pos = Vec2(game.playerPos.x + cosf(angle) * spawnDist,
                        game.playerPos.y + sinf(angle) * spawnDist);
            
            Vec2 toPlayer = (game.playerPos - e.pos).normalized();
            float speed = 1.5f + randf() * 1.0f;
            e.baseSpeed = speed;
            e.vel = toPlayer * speed;
            e.radius = 14.0f + randf() * 10.0f;
            e.alive = true;
            e.hitTimer = 0;
            e.blowAwayTimer = 0;
            e.blowAwayVel = Vec2(0, 0);
            e.beingBlown = false;
            game.enemies.push_back(e);
        }
    }
    
    game.spawnTimer -= dt;
    if (aliveCount < game.maxEnemies && game.spawnTimer <= 0) {
        Enemy e;
        float angle = randf() * 2.0f * M_PI;
        float baseDist = brightness > 0.6f ? 500.0f : 400.0f;
        float spawnDist = baseDist + randf() * 300.0f;
        e.pos = Vec2(game.playerPos.x + cosf(angle) * spawnDist,
                    game.playerPos.y + sinf(angle) * spawnDist);
        
        Vec2 toPlayer = (game.playerPos - e.pos).normalized();
        float speed = 1.5f + randf() * 1.0f + brightness * 0.5f;
        e.baseSpeed = speed;
        e.vel = toPlayer * speed;
        e.radius = 14.0f + randf() * 10.0f;
        e.alive = true;
        e.hitTimer = 0;
        e.blowAwayTimer = 0;
        e.blowAwayVel = Vec2(0, 0);
        e.beingBlown = false;
        game.enemies.push_back(e);
        game.spawnTimer = 0.8f + randf() * 1.5f;
    }
    
    // Sword animation
    if (game.jumping) {
        float distToTarget = (game.jumpTarget - game.playerPos).len();
        float startDist = (game.jumpTarget - game.jumpStart).len();
        float approachProgress = 1.0f - distToTarget / (startDist + 0.001f);
        
        if (!game.hasSlashed) {
            float windupProgress = approachProgress;
            if (windupProgress > 1.0f) windupProgress = 1.0f;
            windupProgress = windupProgress * windupProgress * (3.0f - 2.0f * windupProgress);
            float targetOffset = -M_PI * 0.89f;
            game.swordOffset = game.swordOffset + (targetOffset - game.swordOffset) * dt * 8.0f;
            if (distToTarget < 70.0f) game.hasSlashed = true;
        } else {
            float slashProgress = (game.jumpDuration - game.jumpTimer) / (game.jumpDuration * 0.4f);
            if (slashProgress > 1.0f) slashProgress = 1.0f;
            slashProgress = 1.0f - (1.0f - slashProgress) * (1.0f - slashProgress);
            float startOffset = -M_PI * 0.89f;
            float endOffset = M_PI * 0.67f;
            float targetOffset = startOffset + (endOffset - startOffset) * slashProgress;
            game.swordOffset = game.swordOffset + (targetOffset - game.swordOffset) * dt * 25.0f;
        }
        game.swordAngle = game.playerAngle + game.swordOffset;
    } else {
        float idlePhase = game.gameTime * 2.5f;
        float targetOffset = sinf(idlePhase) * 0.3f;
        float diff = angleDiff(targetOffset, game.swordOffset);
        game.swordOffset += diff * dt * 6.0f;
        game.swordAngle = game.playerAngle + game.swordOffset;
    }
    
    // Update jump
    if (game.jumping) {
        game.jumpTimer -= dt;
        
        float t = 1.0f - (game.jumpTimer / game.jumpDuration);
        t = t < 0.5f ? 2.0f * t * t : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) * 0.5f;
        
        game.playerPos = bezier(t, game.jumpStart, game.jumpControl, game.jumpTarget);
    }
    
    // Always update angle to face movement
    Vec2 moveDir = game.playerPos - game.jumpStart;
    if (game.jumping && moveDir.len() > 0.01f) {
        game.playerAngle = atan2f(moveDir.y, moveDir.x);
    }
    
    // Calculate sword base and tip positions for ribbon trail
    Vec2 swordBase = game.playerPos;
    Vec2 swordTip(
        game.playerPos.x + cosf(game.swordAngle) * 115.0f,
        game.playerPos.y + sinf(game.swordAngle) * 115.0f
    );
    
    // Add ribbon trail - stores full sword line (base + tip)
    // Interpolate multiple points for smooth curtain effect
    float ghostBrightness = brightness;
    int segments = game.jumping ? 8 : 2;
    for (int i = 0; i < segments; i++) {
        SwordRibbon ribbon;
        float t = i / (float)segments;
        // Current base and tip
        ribbon.base = swordBase;
        ribbon.tip = swordTip;
        ribbon.lifetime = 0.5f;
        ribbon.maxLifetime = 0.5f;
        ribbon.brightness = ghostBrightness;
        game.swordRibbons.push_back(ribbon);
    }
    
    // Kill during slash
    if (game.jumping && game.hasSlashed) {
        for (auto& e : game.enemies) {
            if (!e.alive) continue;
            float d = (e.pos - swordTip).len();
            if (d < e.radius + 45.0f) {
                blowAwayEnemies(game, e.pos);
                e.alive = false;
                e.hitTimer = 0.15f;
            }
        }
    }
    
    if (game.jumping && game.jumpTimer <= 0) {
        game.jumping = false;
        game.playerPos = game.jumpTarget;
        game.playerVel = Vec2(0, 0);
        
        if (game.targetEnemy >= 0 && game.targetEnemy < (int)game.enemies.size() &&
            game.enemies[game.targetEnemy].alive) {
            blowAwayEnemies(game, game.enemies[game.targetEnemy].pos);
            game.enemies[game.targetEnemy].alive = false;
            game.enemies[game.targetEnemy].hitTimer = 0.15f;
        }
        game.targetEnemy = -1;
        game.hasSlashed = false;
        game.swordActive = false;
    }
    
    // Update enemies
    for (auto& e : game.enemies) {
        if (!e.alive) continue;
        
        if (e.beingBlown && e.blowAwayTimer > 0) {
            e.blowAwayTimer -= dt;
            e.pos = e.pos + e.blowAwayVel * dt;
            e.blowAwayVel = e.blowAwayVel * (1.0f - dt * 2.0f);
            if (e.blowAwayTimer <= 0) {
                e.beingBlown = false;
                e.blowAwayVel = Vec2(0, 0);
            }
        } else {
            float rhythmSpeed = e.baseSpeed * (0.5f + brightness * 1.2f);
            Vec2 toPlayer = (game.playerPos - e.pos).normalized();
            e.vel.x += toPlayer.x * 0.04f * rhythmSpeed;
            e.vel.y += toPlayer.y * 0.04f * rhythmSpeed;
            float speed = e.vel.len();
            if (speed > rhythmSpeed * 1.5f) e.vel = e.vel.normalized() * rhythmSpeed * 1.5f;
            e.pos = e.pos + e.vel * dt * 60.0f;
        }
        if (e.hitTimer > 0) e.hitTimer -= dt;
    }
    
    // Remove dead
    game.enemies.erase(
        std::remove_if(game.enemies.begin(), game.enemies.end(),
            [](const Enemy& e) { return !e.alive && e.hitTimer <= 0; }),
        game.enemies.end()
    );
    
    // Fade sword ribbons
    for (auto& sr : game.swordRibbons) sr.lifetime -= dt;
    game.swordRibbons.erase(
        std::remove_if(game.swordRibbons.begin(), game.swordRibbons.end(),
            [](const SwordRibbon& r) { return r.lifetime <= 0; }),
        game.swordRibbons.end()
    );
    
    // Camera
    Vec2 diff = game.playerPos - game.camera;
    game.camera = game.camera + diff * dt * 5.0f;
    float targetZoom = game.jumping ? 1.15f : 1.3f;
    game.cameraZoom += (targetZoom - game.cameraZoom) * dt * 4.0f;
    if (game.screenShake > 0) game.screenShake -= dt * 5.0f;
}

Vec2 getWorldToScreen(const GameState& game, const Vec2& worldPos, int screenW, int screenH) {
    float shakeX = 0, shakeY = 0;
    if (game.screenShake > 0) {
        float angle = randf() * 2.0f * M_PI;
        shakeX = cosf(angle) * game.screenShake * 10.0f;
        shakeY = sinf(angle) * game.screenShake * 10.0f;
    }
    float zoom = game.cameraZoom;
    float sx = (worldPos.x - game.camera.x) * zoom + screenW * 0.5f + shakeX;
    float sy = (worldPos.y - game.camera.y) * zoom + screenH * 0.45f + shakeY;
    return Vec2(sx, sy);
}
