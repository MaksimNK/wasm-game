#pragma once

#include "components.hpp"
#include "events.hpp"
#include "audio.hpp"
#include "model_loader.hpp"

// ============================================================================
// VISUAL FRAME
// ============================================================================

struct VisualFrame {
	Vec2 player_pos;
	float player_angle = 0;
	float sword_angle = 0;
	float sword_pitch = 0;
	
	std::vector<SwordRibbon> ribbons;
	Camera camera;
	
	struct VisualEnemy {
		Vec2 pos;
		float radius = 0;
		float alpha = 1.0f;
		bool alive = true;
	};
	std::vector<VisualEnemy> enemies;
	
	float score_fill = 0;
	int score_level = 0;
	float score_feedback_timer = 0;
	bool score_feedback_good = false;
	float score_bar_bounce = 0;
	int nearest_enemy_idx = -1;

	// Sword model (world-space, flat triangle list)
	std::vector<Vec3> sword_verts_world;
	Vec3 sword_tip_world;
	bool has_sword_model = false;

	// Ripple VFX
	std::vector<Ripple> ripples;
};

// ============================================================================
// GAME STATE
// ============================================================================

struct GameState {
	Player player;
	Sword sword;
	Camera camera;
	std::vector<Enemy> enemies;
	ScoreData score;
	MeshModel sword_model;
	float sword_scale = 115.0f;
	float time_scale = 1.0f;
	float game_time = 0;
	float music_time = 0;
	bool running = false;
	float spawn_timer = 0;
	std::vector<Ripple> ripples;

	void init();
};

// ============================================================================
// SYSTEMS
// ============================================================================

namespace Systems {
	bool poll_input(EventBus& events);
	
	// Merged player + sword system (replaces update_combat + update_movement)
	void update_player_flow(GameState& game, EventBus& events, float dt,
	                        const Timeline& timeline, float music_time, float gradient);
	
	// Enemy behavior (unchanged except no longer reads player.state)
	void update_enemies(GameState& game, float dt);
	
	void update_spawn(GameState& game, float dt, float gradient);
	void update_score(GameState& game, EventBus& events, float dt, float gradient);
	void update_ripples(GameState& game, EventBus& events, float dt);
	void build_visual_frame(GameState& game, float dt, VisualFrame& out);
	void update_camera(Camera& camera, const Vec2& target_pos, bool player_jumping, float dt);
}

float get_brightness_at_time(const Timeline& timeline, float time);
float get_time_scale(float brightness);
Vec2 world_to_screen(const Vec2& world_pos, const Camera& camera, int screen_w, int screen_h);
bool is_good_timing(const Timeline& timeline, float music_time);
void get_score_rank_color(int level, float& r, float& g, float& b);
const char* get_score_rank_name(int level);
