#include "components.hpp"

void Player::reset() {
	pos = Vec2(120, 80);
	vel = Vec2(30, 20);
	angle = M_PI * 0.25f;
	trajectory.reset();
	is_gliding = false;
	has_struck = false;
	target_enemy = -1;
}

void Sword::reset() {
	offset_angle = 0.0f;
	visual_offset = 0.0f;
	pitch = 0.0f;
	visual_pitch = 0.0f;
	state = SwordState::Idle;
	state_progress = 0.0f;
	slash_from_left = true;
	ribbons.clear();
}

void Enemy::reset() {
	alive = true;
	flash_timer = 0;
	blow_away_timer = 0;
	blow_away_vel = Vec2(0, 0);
	being_blown = false;
	fear_timer = 0;
	behavior = EnemyBehavior::Chase;
	behavior_timer = 0;
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
	bar_bounce = 0;
}
