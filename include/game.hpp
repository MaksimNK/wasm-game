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

struct SwordRibbon {
    Vec2 base;
    Vec2 tip;
    float lifetime;
    float maxLifetime;
    float gradient;
};

// --- Game Object Hierarchy ---

// Base class for all game entities
struct GameObject {
    Vec2 pos;
    float angle;
    bool active;
    
    GameObject() : pos(0, 0), angle(0), active(true) {}
    virtual ~GameObject() = default;
};

// Player entity
struct Player : GameObject {
    bool jumping;
    Vec2 jumpTarget;
    Vec2 jumpStart;
    Vec2 jumpControl;
    float jumpTimer;
    float jumpDuration;
    float swordOffset;
    bool hasSlashed;
    std::vector<SwordRibbon> swordRibbons;
    
    Player();
    void reset();
};

// Enemy entity
struct Enemy : GameObject {
    Vec2 vel;
    float baseSpeed;
    float radius;
    bool alive;
    float flashTimer;
    float blowAwayTimer;
    Vec2 blowAwayVel;
    bool beingBlown;
    
    Enemy();
    void reset();
};

struct Camera {
    Vec2 pos;
    float zoom;
};

struct GameState {
    Player player;
    Camera camera;
    std::vector<Enemy> enemies;
    float timeScale;
    float gameTime;
    float musicTime;
    bool running;
    float spawnTimer;
    int maxEnemies;
    int targetEnemy;
};

float getBrightnessAtTime(const Timeline& timeline, float time);
float getTimeScale(float brightness);
void initGame(GameState& game, int screenW, int screenH);
void updateGame(GameState& game, const Timeline& timeline, float realDt, float musicTime);
void processAttack(GameState& game, const Timeline& timeline);
Vec2 getWorldToScreen(const GameState& game, const Vec2& worldPos, int screenW, int screenH);
