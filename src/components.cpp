#include "components.hpp"

void Player::reset() {
    pos = Vec2(0, 0);
    angle = 0;
    state = EntityState::Idle;
    state_timer = 0;
    state_duration = 0.25f;
    jump_start = jump_target = jump_control = Vec2(0, 0);
    sword_offset = 0;
    ribbons.clear();
    can_chain = false;
    target_enemy = -1;
    has_slashed = false;
}

void Enemy::reset() {
    alive = true;
    flash_timer = 0;
    blow_away_timer = 0;
    blow_away_vel = Vec2(0, 0);
    being_blown = false;
}

void ScoreData::reset() {
    points = 0;
    level = 0;
    combo = 0;
    misses = 0;
    fill = 0;
    last_hit_good = false;
    feedback_timer = 0;
    display_points = 0;
    display_fill = 0;
    display_level = 0;
    level_anim_timer = 0;
}
