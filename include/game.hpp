#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include "audio.hpp"

struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float x, float y) : x(x), y(y) {}
    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
    float len() const { return sqrtf(x*x + y*y); }
    Vec2 normalized() const { float l = len(); return l > 0.001f ? Vec2(x/l, y/l) : Vec2(0,0); }
};

struct Enemy {
    Vec2 pos;
    Vec2 vel;
    float baseSpeed;
    float radius;
    bool alive;
    float hitTimer;
    float blowAwayTimer;
    Vec2 blowAwayVel;
    bool beingBlown;
};

struct Wall {
    float x, y, w, h;
};

// Ribbon trail - stores the full sword line (base + tip) for mesh-like trail
struct SwordRibbon {
    Vec2 base;       // Sword base position
    Vec2 tip;        // Sword tip position
    float lifetime;
    float maxLifetime;
    float brightness;
};

struct GameState {
    Vec2 playerPos;
    Vec2 playerVel;
    float playerAngle;
    std::vector<Enemy> enemies;
    std::vector<Wall> walls;
    Vec2 camera;
    float cameraZoom;
    float timeScale;
    float gameTime;
    float musicTime;
    int score;
    int combo;
    bool jumping;
    Vec2 jumpTarget;
    Vec2 jumpStart;
    Vec2 jumpControl;
    float jumpTimer;
    float jumpDuration;
    std::vector<SwordRibbon> swordRibbons;
    float screenShake;
    float lastAttackTime;
    float attackCooldown;
    bool running;
    float spawnTimer;
    float swordAngle;
    float swordOffset;
    bool swordActive;
    bool hasSlashed;
    int maxEnemies;
    int targetEnemy;
};

float getBrightnessAtTime(const Timeline& timeline, float time);
float getTimeScale(float brightness);
float getBarOpacity(float brightness);
void initGame(GameState& game, int screenW, int screenH);
void updateGame(GameState& game, const Timeline& timeline, float realDt, float musicTime);
void processAttack(GameState& game, const Timeline& timeline);
Vec2 getWorldToScreen(const GameState& game, const Vec2& worldPos, int screenW, int screenH);
