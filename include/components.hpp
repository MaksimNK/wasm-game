#pragma once

#include "types.hpp"
#include <vector>

struct SwordRibbon {
    Vec2 base, tip;
    float lifetime = 0;
    float max_lifetime = 0;
    float intensity = 0;
};

enum class EntityState { Idle, Charging, Slashing };

struct Player {
    Vec2 pos;
    Vec2 vel;
    float angle = 0;
    float target_angle = 0;
    EntityState state = EntityState::Idle;
    float state_timer = 0;
    float state_duration = 0.25f;
    Vec2 jump_start, jump_target, jump_control;
    float sword_offset = 0;
    std::vector<SwordRibbon> ribbons;
    bool can_chain = false;
    int target_enemy = -1;
    bool has_slashed = false;
    
    void reset();
};

enum class EnemyBehavior { Chase, Flee, Charge };

struct Enemy {
    Vec2 pos;
    Vec2 vel;
    float radius = 20;
    bool alive = true;
    float flash_timer = 0;
    float blow_away_timer = 0;
    Vec2 blow_away_vel;
    float base_speed = 0;
    bool being_blown = false;
    float fear_timer = 0;
    EnemyBehavior behavior = EnemyBehavior::Chase;
    float behavior_timer = 0;
    
    void reset();
};

struct Camera {
    Vec2 pos;
    float zoom = 1.3f;
};

struct ScoreData {
    int points = 0;
    int level = 0;
    int combo = 0;
    int misses = 0;
    float fill = 0;
    bool last_hit_good = false;
    float feedback_timer = 0;
    float display_points = 0;
    float display_fill = 0;
    int display_level = 0;
    float level_anim_timer = 0;
    float bar_bounce = 0;
    
    void reset();
};
