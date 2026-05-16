#pragma once

#include "types.hpp"
#include <vector>

struct SwordRibbon {
	Vec2 base, tip;
	float lifetime = 0;
	float max_lifetime = 0;
	float intensity = 0;
};

// ============================================================================
// TRAJECTORY
// ============================================================================

struct Trajectory {
	Vec2 p0, p1, p2;
	float length = 0.0f;
	float progress = 0.0f;
	int target_enemy = -1;
	bool active = false;
	
	Vec2 position() const {
		float u = 1.0f - progress;
		return p0 * (u * u) + p1 * (2.0f * u * progress) + p2 * (progress * progress);
	}
	
	Vec2 tangent() const {
		float u = 1.0f - progress;
		return (p1 - p0) * (2.0f * u) + (p2 - p1) * (2.0f * progress);
	}
	
	void reset() {
		active = false;
		progress = 0.0f;
		target_enemy = -1;
		length = 0.0f;
	}
};

// ============================================================================
// SWORD
// ============================================================================

struct Sword {
	float angle = 0.0f;        // absolute world angle
	float visual_angle = 0.0f; // smoothed for rendering
	float pitch = 0.0f;
	float visual_pitch = 0.0f;
	float angular_vel = 0.0f;  // spin velocity during flight
	
	std::vector<SwordRibbon> ribbons;
	
	void reset();
};

// ============================================================================
// PLAYER
// ============================================================================

struct Player {
	Vec2 pos;
	Vec2 vel;
	float angle = 0.0f;
	
	Trajectory trajectory;
	bool is_gliding = false;
	int target_enemy = -1;
	
	void reset();
};

// ============================================================================
// ENEMY
// ============================================================================

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

// ============================================================================
// CAMERA
// ============================================================================

struct Camera {
	Vec2 pos;
	float zoom = 1.3f;
};

// ============================================================================
// SCORE
// ============================================================================

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
