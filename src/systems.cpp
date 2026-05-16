#include "systems.hpp"
#include <SDL.h>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// CONSTANTS
// ============================================================================

static constexpr float TIME_SCALE_MIN = 0.15f;
static constexpr float TIME_SCALE_MAX = 1.0f;
static constexpr float BASE_ENEMY_COUNT = 5.0f;
static constexpr float ENEMY_COUNT_SCALE = 3.0f;
static constexpr float SPAWN_DIST_MIN = 900.0f;
static constexpr float SPAWN_DIST_VAR = 300.0f;
static constexpr float MIN_SPAWN_SPEED = 8.0f;
static constexpr float MAX_SPAWN_SPEED = 16.0f;
static constexpr float MIN_ENEMY_RADIUS = 14.0f;
static constexpr float MAX_ENEMY_RADIUS = 24.0f;
static constexpr float SPAWN_TIMER_MIN = 0.3f;
static constexpr float SPAWN_TIMER_MAX = 0.8f;
static constexpr float MIN_ATTACK_DIST = 40.0f;
static constexpr float SWORD_LENGTH = 150.0f;
static constexpr float SWORD_HIT_RANGE = 150.0f;
static constexpr float BLOW_RADIUS = 300.0f;
static constexpr float BLOW_FORCE = 400.0f;
static constexpr float BLOW_DURATION = 0.6f;
static constexpr float FLASH_DURATION = 0.15f;
static constexpr float ENEMY_SPEED = 440.0f;
static constexpr float ENEMY_MAX_SPEED = 700.0f;
static constexpr float ENEMY_FEAR_SPEED = 1200.0f;
static constexpr float GLIDE_FRICTION = 0.1f;
static constexpr float GLIDE_MIN_SPEED = 33.0f;
static constexpr float ZOOM_IDLE = 1.33f;
static constexpr float ZOOM_JUMP = 1.00f;
static constexpr float SCREEN_CENTER_X = 0.5f;
static constexpr float SCREEN_CENTER_Y = 0.5f;
static constexpr float TIMING_THRESHOLD = 0.33f;
static constexpr float TIMING_WINDOW_SEC = 0.33f;
static constexpr int BASE_HIT_POINTS = 100;
static constexpr int KILL_POINTS = 50;
static constexpr float FILL_PER_HIT = 0.15f;
static constexpr float MISS_PENALTY = 0.15f;
static constexpr float LEVEL_MULTIPLIER = 0.1f;
static constexpr int MAX_LEVEL = 7;

// Flight speed: base + gradient scaling
static constexpr float BASE_FLIGHT_SPEED = 1000.0f;
static constexpr float SPEED_GRADIENT_SCALE = 2.7f;

// Turn rate clamp (radians/sec)
static constexpr float MAX_TURN_RATE = M_PI * 0.5f;

// Sword behavior
static constexpr float SWORD_SLASH_BOOST = 77.0f;
static constexpr float SWORD_ANGLE_SMOOTH = 15.0f;
static constexpr float CAMERA_FOLLOW_SPEED = 8.0f;
static constexpr float CAMERA_ZOOM_SPEED = 10.0f;

// Ribbons
static constexpr float RIBBON_LIFETIME = 1.0f;

// ============================================================================
// HELPERS
// ============================================================================

float get_brightness_at_time(const Timeline& timeline, float time) {
	if (timeline.gradient.empty() || timeline.sample_rate <= 0) return 0.0f;
	int idx = (int)(time * timeline.sample_rate);
	if (idx < 0) return timeline.gradient[0];
	if (idx >= (int)timeline.gradient.size()) return timeline.gradient.back();
	return timeline.gradient[idx];
}

float get_time_scale(float brightness) {
	return TIME_SCALE_MIN + (TIME_SCALE_MAX - TIME_SCALE_MIN) * brightness;
}

Vec2 world_to_screen(const Vec2& world_pos, const Camera& camera, int screen_w, int screen_h) {
	return Vec2(
		(world_pos.x - camera.pos.x) * camera.zoom + screen_w * SCREEN_CENTER_X,
		(world_pos.y - camera.pos.y) * camera.zoom + screen_h * SCREEN_CENTER_Y
	);
}

bool is_good_timing(const Timeline& timeline, float music_time) {
	if (timeline.gradient.empty() || timeline.sample_rate <= 0) return false;
	int idx = (int)(music_time * timeline.sample_rate);
	int window = (int)(timeline.sample_rate * TIMING_WINDOW_SEC);
	if (idx < 0 || idx >= (int)timeline.gradient.size()) return false;
	
	float peak = 0.0f;
	for (int i = idx - window; i <= idx + window; i++) {
		if (i >= 0 && i < (int)timeline.gradient.size()) {
			peak = fmaxf(peak, timeline.gradient[i]);
		}
	}
	
	return peak >= TIMING_THRESHOLD && timeline.gradient[idx] >= peak * 0.85f;
}

const char* get_score_rank_name(int level) {
	switch (level) {
		case 0: return "F";
		case 1: return "E";
		case 2: return "D";
		case 3: return "C";
		case 4: return "B";
		case 5: return "A";
		case 6: return "S";
		case 7: return "S+";
		default: return "F";
	}
}

void get_score_rank_color(int level, float& r, float& g, float& b) {
	switch (level) {
		case 0: r = 0.55f; g = 0.10f; b = 0.10f; break;
		case 1: r = 0.65f; g = 0.12f; b = 0.12f; break;
		case 2: r = 0.75f; g = 0.15f; b = 0.15f; break;
		case 3: r = 0.82f; g = 0.18f; b = 0.18f; break;
		case 4: r = 0.88f; g = 0.22f; b = 0.22f; break;
		case 5: r = 0.93f; g = 0.28f; b = 0.28f; break;
		case 6: r = 0.97f; g = 0.35f; b = 0.35f; break;
		case 7: r = 1.00f; g = 0.45f; b = 0.45f; break;
		default: r = 0.55f; g = 0.10f; b = 0.10f; break;
	}
}

static float get_miss_penalty(int level, int misses) {
	float base = 0.85f - level * 0.08f;
	if (base < 0.15f) base = 0.15f;
	float decay = 1.0f;
	for (int i = 0; i < misses; i++) decay *= base;
	return decay;
}

static bool point_in_convex_poly(const Vec2& p, const Vec2* poly, int n) {
	for (int i = 0; i < n; i++) {
		Vec2 a = poly[i];
		Vec2 b = poly[(i+1)%n];
		float cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
		if (cross < -0.001f) return false;
	}
	return true;
}

static bool circle_intersects_poly(const Vec2& center, float radius, const Vec2* poly, int n) {
	if (point_in_convex_poly(center, poly, n)) return true;
	for (int i = 0; i < n; i++) {
		Vec2 a = poly[i];
		Vec2 b = poly[(i+1)%n];
		Vec2 ab = b - a;
		Vec2 ac = center - a;
		float ab_len_sq = ab.x * ab.x + ab.y * ab.y;
		float t = ab_len_sq > 0.001f ? fmaxf(0.0f, fminf(1.0f, (ac.x * ab.x + ac.y * ab.y) / ab_len_sq)) : 0.0f;
		Vec2 closest = a + ab * t;
		if ((center - closest).len() < radius) return true;
	}
	return false;
}

static Mat4 build_sword_matrix(const Vec2& pos, float angle, float pitch, float scale) {
	Mat4 m = Mat4::scale(scale, scale, scale);
	m = multiply(Mat4::rotate_x(pitch), m);
	m = multiply(Mat4::rotate_z(angle), m);
	m = multiply(Mat4::translate(pos.x, pos.y, 0.0f), m);
	return m;
}

// ============================================================================
// GAME STATE
// ============================================================================

void GameState::init() {
	srand(42);
	player.reset();
	sword.reset();
	camera.pos = Vec2(0, 0);
	camera.zoom = ZOOM_IDLE;
	running = true;
	spawn_timer = 0;
	time_scale = 1.0f;
	game_time = 0;
	music_time = 0;
	enemies.clear();
	score.reset();
	if (sword_model.vertices.empty()) {
		if (!sword_model.load_gltf("static/models/sword.gltf")) {
			fprintf(stderr, "[WARN] Failed to load sword model\n");
		} else {
			sword_scale = 150.0f * sword_model.default_scale;
		}
	}
}

// ============================================================================
// INPUT
// ============================================================================

bool Systems::poll_input(EventBus& events) {
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) return false;
		bool trigger = (e.type == SDL_KEYDOWN) ||
					   (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) ||
					   (e.type == SDL_FINGERDOWN);
		if (trigger) events.attacks.push_back({});
	}
	return true;
}

// ============================================================================
// PLAYER FLOW (merged combat + movement)
// ============================================================================

static int find_nearest_enemy(const Vec2& pos, const std::vector<Enemy>& enemies) {
	int nearest = -1;
	float min_dist = 999999.0f;
	for (size_t i = 0; i < enemies.size(); i++) {
		if (!enemies[i].alive) continue;
		float d = (enemies[i].pos - pos).len();
		if (d < min_dist && d > MIN_ATTACK_DIST) {
			min_dist = d;
			nearest = (int)i;
		}
	}
	return nearest;
}

static void blow_away_enemies(std::vector<Enemy>& enemies, Vec2 center) {
	for (auto& en : enemies) {
		if (!en.alive) continue;
		Vec2 diff = en.pos - center;
		float dist = diff.len();
		if (dist < BLOW_RADIUS && dist > 0.001f) {
			Vec2 dir = diff.normalized();
			float force = BLOW_FORCE * (1.0f - dist / BLOW_RADIUS);
			en.blow_away_vel = dir * force;
			en.blow_away_timer = BLOW_DURATION;
			en.being_blown = true;
		}
	}
}

static void build_trajectory(Trajectory& traj, const Vec2& start, const Vec2& enemy_pos,
							 const Vec2& current_vel, float sword_scale) {
	traj.active = true;
	traj.progress = 0.0f;
	
	Vec2 dir = (enemy_pos - start).normalized();
	float dist = (enemy_pos - start).len();
	
	// Land BEHIND the enemy (fly past them)
	float fly_past_dist = fminf(dist * 0.33f, SWORD_LENGTH * 3);
	Vec2 end = enemy_pos + dir * fly_past_dist;
	
	// C1 continuity: use current velocity direction as initial tangent
	Vec2 tang = current_vel.len() > 5.0f ? current_vel.normalized() : dir;
	float curve_dist = (end - start).len();
	
	// Control point: along tangent with lateral curve
	Vec2 mid = start + tang * (curve_dist * 0.5f);
	Vec2 perp(-tang.y, tang.x);
	float curve_side = (randf() > 0.5f ? 1.0f : -1.0f);
	traj.p0 = start;
	// traj.p1 = mid + perp * CURVE_AMOUNT * curve_side;
	traj.p1 = mid + perp * curve_side;
	traj.p2 = end;
	
	// Approximate curve length for speed-based progress
	traj.length = 0.0f;
	Vec2 prev = traj.p0;
	for (int i = 1; i <= 20; i++) {
		float t = i / 20.0f;
		Vec2 curr = bezier(t, traj.p0, traj.p1, traj.p2);
		traj.length += (curr - prev).len();
		prev = curr;
	}
	if (traj.length < 1.0f) traj.length = 1.0f;
}

static void check_sword_collision(GameState& game, Sword& sword, Player& player) {
	if (game.sword_model.collision_poly.empty()) return;
	
	Mat4 sword_mat = build_sword_matrix(player.pos, sword.angle, sword.pitch, game.sword_scale);
	
	Vec2 world_poly[4];
	for (int i = 0; i < 4; i++) {
		Vec3 v = transform_point(sword_mat, Vec3(game.sword_model.collision_poly[i].x,
											  game.sword_model.collision_poly[i].y, 0.0f));
		world_poly[i] = Vec2(v.x, v.y);
	}
	
	for (size_t i = 0; i < game.enemies.size(); i++) {
		auto& en = game.enemies[i];
		if (!en.alive) continue;
		float hit_dist = en.radius;
		if (circle_intersects_poly(en.pos, hit_dist, world_poly, 4)) {
			blow_away_enemies(game.enemies, en.pos);
			en.alive = false;
			en.flash_timer = FLASH_DURATION;
			float sword_side = (randf() > 0.5f ? 1.0f : -1.0f);
			sword.angular_vel = SWORD_SLASH_BOOST * sword_side;
		}
	}
}

static void spawn_ribbons(Sword& s, const Vec2& base, const Vec2& tip, float dt) {
	float intensity = dt * 33.0f + 0.33f;

    SwordRibbon r;
	r.base = base;
	r.tip = tip;
	r.lifetime = RIBBON_LIFETIME;
	r.max_lifetime = RIBBON_LIFETIME;
	r.intensity = intensity;
	s.ribbons.push_back(r);
	
	for (auto& sr : s.ribbons) sr.lifetime -= dt;
	s.ribbons.erase(
		std::remove_if(s.ribbons.begin(), s.ribbons.end(),
			[](const SwordRibbon& r) { return r.lifetime <= 0; }),
		s.ribbons.end()
	);
}

void Systems::update_player_flow(GameState& game, EventBus& events, float dt,
								 const Timeline& timeline, float music_time, float gradient) {
	Player& p = game.player;
	Sword& s = game.sword;
	
	// --- Process input ---
	for (const auto& evt : events.attacks) {
		(void)evt;
		
		// Block: already flying
		if (p.trajectory.active) continue;
		
		int target = find_nearest_enemy(p.pos, game.enemies);
		if (target < 0) continue;
		
		bool good_timing = is_good_timing(timeline, music_time);
		
		// Build trajectory to target
		build_trajectory(p.trajectory, p.pos, game.enemies[target].pos, p.vel, game.sword_scale);
		p.trajectory.target_enemy = target;
		p.target_enemy = target;
		
		events.scores.push_back({good_timing, 1});
	}
	
	// --- Movement along trajectory ---
	if (p.trajectory.active) {
		// Speed scales with music intensity
		float flight_speed = BASE_FLIGHT_SPEED * (1.0f + gradient * SPEED_GRADIENT_SCALE);
		float progress_delta = (flight_speed / p.trajectory.length) * dt;
		p.trajectory.progress += progress_delta;
		
		if (p.trajectory.progress >= 1.0f) {
			// Trajectory complete — fly past the enemy
			p.pos = p.trajectory.p2;
			p.vel = p.trajectory.tangent().normalized() * flight_speed;
			p.trajectory.active = false;
			p.is_gliding = true;
			p.target_enemy = -1;
		} else {
			// Move along curve
			Vec2 prev_pos = p.pos;
			p.pos = p.trajectory.position();
			p.vel = (p.pos - prev_pos) * (1.0f / dt);
			
			// Angle follows tangent with turn rate clamp
			Vec2 tang = p.trajectory.tangent();
			if (tang.len() > 0.001f) {
				float target_angle = atan2f(tang.y, tang.x);
				float diff = angle_diff(target_angle, p.angle);
				float max_turn = MAX_TURN_RATE * dt;
				if (diff > max_turn) diff = max_turn;
				if (diff < -max_turn) diff = -max_turn;
				p.angle += diff;
			}
		}
	} else if (p.is_gliding) {
		// Glide: maintain momentum with velocity-dependent friction
		p.pos = p.pos + p.vel * dt;
		
		float speed = p.vel.len();
		float friction = GLIDE_FRICTION * (1.0f + speed / 250.0f);
		if (friction > 0.9f) friction = 0.9f;
		p.vel = p.vel * (1.0f - dt * friction);
		
		// Angle follows velocity with turn rate clamp
		if (speed > GLIDE_MIN_SPEED) {
			float target_angle = atan2f(p.vel.y, p.vel.x);
			float diff = angle_diff(target_angle, p.angle);
			float max_turn = MAX_TURN_RATE * dt;
			if (diff > max_turn) diff = max_turn;
			if (diff < -max_turn) diff = -max_turn;
			p.angle += diff;
		}
	}
	
	s.angle += s.angular_vel * dt;
	s.angular_vel *= (1.0f - dt * 15.0f);
	
	check_sword_collision(game, s, p);
	
	// Smooth visual interpolation
	float diff = angle_diff(s.angle, s.visual_angle);
	s.visual_angle += diff * dt * SWORD_ANGLE_SMOOTH;
	
	// Ribbons
	Mat4 sword_mat = build_sword_matrix(p.pos, s.visual_angle, s.visual_pitch, game.sword_scale);
	Vec3 tip = transform_point(sword_mat, game.sword_model.tip_vertex);
	spawn_ribbons(s, p.pos, Vec2(tip.x, tip.y), dt);
	
	// --- Flash timers & cleanup ---
	for (auto& en : game.enemies) {
		if (en.flash_timer > 0) en.flash_timer -= dt;
	}
	
	game.enemies.erase(
		std::remove_if(game.enemies.begin(), game.enemies.end(),
			[](const Enemy& e) { return !e.alive && e.flash_timer <= 0 && e.blow_away_timer <= 0; }),
		game.enemies.end()
	);
	
	if (p.target_enemy >= (int)game.enemies.size()) {
		p.target_enemy = -1;
	}
}

// ============================================================================
// ENEMIES
// ============================================================================

void Systems::update_enemies(GameState& game, float dt) {
	const Player& p = game.player;
	
	for (auto& en : game.enemies) {
		if (en.being_blown && en.blow_away_timer > 0) {
			en.blow_away_timer -= dt;
			en.pos = en.pos + en.blow_away_vel * dt;
			en.blow_away_vel = en.blow_away_vel * (1.0f - dt * 2.0f);
			if (en.blow_away_timer <= 0) {
				en.being_blown = false;
				en.fear_timer = 1.0f + randf() * 0.8f;
				en.behavior_timer = 0;
				en.vel = en.blow_away_vel * 0.3f;
				en.blow_away_vel = Vec2(0, 0);
			}
		} else if (en.fear_timer > 0 && en.alive) {
			en.fear_timer -= dt;
			Vec2 away = (en.pos - p.pos).normalized();
			float panic_jitter = (randf() - 0.5f) * M_PI * 1.2f;
			float cos_p = cosf(panic_jitter);
			float sin_p = sinf(panic_jitter);
			Vec2 panic_dir(away.x * cos_p - away.y * sin_p, away.x * sin_p + away.y * cos_p);
			en.vel.x += panic_dir.x * ENEMY_FEAR_SPEED * 4.0f * dt;
			en.vel.y += panic_dir.y * ENEMY_FEAR_SPEED * 4.0f * dt;
			float speed = en.vel.len();
			float max_fear = ENEMY_FEAR_SPEED * 2.5f;
			if (speed > max_fear) en.vel = en.vel.normalized() * max_fear;
			en.pos = en.pos + en.vel * dt;
			if (en.fear_timer <= 0) {
				en.behavior = EnemyBehavior::Chase;
				en.behavior_timer = 0.3f + randf() * 0.3f;
			}
		} else if (en.alive) {
			// Behavior state machine
			en.behavior_timer -= dt;
			if (en.behavior_timer <= 0) {
				EnemyBehavior old = en.behavior;
				float r = randf();
				if (r < 0.30f) {
					en.behavior = EnemyBehavior::Chase;
					en.behavior_timer = 0.4f + randf() * 0.4f;
				} else if (r < 0.55f) {
					en.behavior = EnemyBehavior::Flee;
					en.behavior_timer = 0.25f + randf() * 0.35f;
				} else {
					en.behavior = EnemyBehavior::Charge;
					en.behavior_timer = 0.15f + randf() * 0.25f;
				}
				if (old != en.behavior) {
					en.vel = en.vel * 0.6f;
				}
			}
			
			Vec2 to_player = (p.pos - en.pos).normalized();
			float accel = ENEMY_SPEED * 3.0f;
			float max_speed = ENEMY_MAX_SPEED * (0.6f + 0.5f * en.base_speed / MAX_SPAWN_SPEED);
			
			switch (en.behavior) {
				case EnemyBehavior::Chase: {
					float jx = (randf() - 0.5f) * 80.0f;
					float jy = (randf() - 0.5f) * 80.0f;
					en.vel.x += (to_player.x * accel + jx) * dt;
					en.vel.y += (to_player.y * accel + jy) * dt;
					max_speed *= 1.1f;
					break;
				}
				case EnemyBehavior::Flee: {
					Vec2 away = (en.pos - p.pos).normalized();
					float jx = (randf() - 0.5f) * 100.0f;
					float jy = (randf() - 0.5f) * 100.0f;
					en.vel.x += (away.x * accel * 2.2f + jx) * dt;
					en.vel.y += (away.y * accel * 2.2f + jy) * dt;
					max_speed *= 1.6f;
					break;
				}
				case EnemyBehavior::Charge: {
					float jx = (randf() - 0.5f) * 40.0f;
					float jy = (randf() - 0.5f) * 40.0f;
					en.vel.x += (to_player.x * accel * 6.0f + jx) * dt;
					en.vel.y += (to_player.y * accel * 6.0f + jy) * dt;
					max_speed *= 2.2f;
					break;
				}
			}
			
			float speed = en.vel.len();
			if (speed > max_speed) en.vel = en.vel.normalized() * max_speed;
			en.pos = en.pos + en.vel * dt;
		}
	}
}

// ============================================================================
// SPAWN
// ============================================================================

static void spawn_enemy(std::vector<Enemy>& enemies, const Vec2& player_pos, float gradient, const Camera& camera) {
	Enemy en;
	float angle = randf() * 2.0f * M_PI;
	float min_visible = 400.0f / camera.zoom;
	float dist = fmaxf(SPAWN_DIST_MIN, min_visible * 1.4f) + randf() * SPAWN_DIST_VAR;
	en.pos = Vec2(player_pos.x + cosf(angle) * dist, player_pos.y + sinf(angle) * dist);
	
	Vec2 to_player = (player_pos - en.pos).normalized();
	en.base_speed = MIN_SPAWN_SPEED + randf() * (MAX_SPAWN_SPEED - MIN_SPAWN_SPEED);
	en.vel = to_player * en.base_speed;
	en.radius = MIN_ENEMY_RADIUS + randf() * (MAX_ENEMY_RADIUS - MIN_ENEMY_RADIUS);
	en.reset();
	enemies.push_back(en);
}

void Systems::update_spawn(GameState& game, float dt, float gradient) {
	int alive_count = 0;
	for (const auto& en : game.enemies) if (en.alive) alive_count++;
	
	int target_count = (int)(BASE_ENEMY_COUNT + gradient * ENEMY_COUNT_SCALE);
	int max_count = (int)(3.0f + gradient * 4.0f);
	
	if (alive_count < target_count) {
		int needed = target_count - alive_count;
		for (int i = 0; i < needed && alive_count + i < max_count; i++) {
			spawn_enemy(game.enemies, game.player.pos, gradient, game.camera);
		}
	}
	
	game.spawn_timer -= dt;
	if (alive_count < max_count && game.spawn_timer <= 0) {
		spawn_enemy(game.enemies, game.player.pos, gradient, game.camera);
		game.spawn_timer = SPAWN_TIMER_MIN + randf() * (SPAWN_TIMER_MAX - SPAWN_TIMER_MIN);
	}
}

// ============================================================================
// SCORE
// ============================================================================

void Systems::update_score(GameState& game, EventBus& events, float dt, float gradient) {
	ScoreData& s = game.score;
	
	for (const auto& evt : events.scores) {
		if (evt.good_hit) {
			int points = BASE_HIT_POINTS * (1 + s.level);
			s.points += points * (1 + s.combo);
			s.fill += FILL_PER_HIT;
			s.combo++;
			s.misses = 0;
			s.last_hit_good = true;
		} else {
			float penalty = get_miss_penalty(s.level, s.misses);
			s.points = (int)(s.points * penalty);
			s.fill -= MISS_PENALTY * (1.0f + s.level * 0.15f);
			s.combo = 0;
			s.misses++;
			s.last_hit_good = false;
		}
		s.feedback_timer = 0.5f;
	}
	
	if (s.feedback_timer > 0) s.feedback_timer -= dt;
	if (s.fill >= 1.0f && s.level < MAX_LEVEL) {
		s.fill = 0.0f;
		s.level++;
		s.combo = 0;
	}
	if (s.fill < 0 && s.level > 0) {
		s.level--;
		s.fill = 0.75f + s.fill;
		if (s.fill < 0) s.fill = 0;
		s.combo = 0;
	} else if (s.fill < 0) {
		s.fill = 0;
	}
	
	s.bar_bounce += (gradient - s.bar_bounce) * dt * 10.0f;
}

// ============================================================================
// ANIMATION / VISUAL FRAME
// ============================================================================

void Systems::build_visual_frame(GameState& game, float dt, VisualFrame& out) {
	Player& p = game.player;
	Sword& s = game.sword;

	// Build frame from player + sword
	out.player_pos = p.pos;
	out.player_angle = p.angle;
	out.sword_angle = s.visual_angle;
	out.camera = game.camera;
	out.enemies.clear();

	for (const auto& en : game.enemies) {
		VisualFrame::VisualEnemy ven;
		ven.pos = en.pos;
		ven.radius = en.radius;
		ven.alive = en.alive;
		if (!en.alive && en.flash_timer > 0) {
			ven.alpha = en.flash_timer / FLASH_DURATION;
		} else if (!en.alive) {
			ven.alpha = 0.5f;
		} else {
			ven.alpha = (en.flash_timer > 0 || en.being_blown) ? 1.0f : 0.7f;
		}
		out.enemies.push_back(ven);
	}

	// Transform sword model to world space
	out.has_sword_model = false;
	out.sword_verts_world.clear();
	if (!game.sword_model.vertices.empty()) {
		Mat4 sword_mat = build_sword_matrix(p.pos, out.sword_angle, out.sword_pitch, game.sword_scale);
		out.sword_verts_world.reserve(game.sword_model.vertices.size());
		for (const auto& v : game.sword_model.vertices) {
			out.sword_verts_world.push_back(transform_point(sword_mat, v));
		}
		out.sword_tip_world = transform_point(sword_mat, game.sword_model.tip_vertex);
		out.has_sword_model = true;
	}

	// Ribbons from sword
	out.ribbons = s.ribbons;

	// UI with smooth animation
	ScoreData& sc = game.score;
	sc.display_points += (sc.points - sc.display_points) * dt * 8.0f;
	sc.display_fill += (sc.fill - sc.display_fill) * dt * 6.0f;

	if (sc.level != sc.display_level) {
		sc.level_anim_timer += dt * 4.0f;
		if (sc.level_anim_timer >= 1.0f) {
			sc.display_level = sc.level;
			sc.level_anim_timer = 0.0f;
		}
	}

	out.score_fill = sc.display_fill;
	out.score_level = sc.display_level;
	out.score_feedback_timer = sc.feedback_timer;
	out.score_feedback_good = sc.last_hit_good;
	out.score_bar_bounce = sc.bar_bounce;

	// Find nearest enemy for UI indicator
	out.nearest_enemy_idx = -1;
	float min_dist = 999999.0f;
	for (size_t i = 0; i < game.enemies.size(); i++) {
		if (!game.enemies[i].alive) continue;
		float d = (game.enemies[i].pos - game.player.pos).len();
		if (d < min_dist && d > MIN_ATTACK_DIST) {
			min_dist = d;
			out.nearest_enemy_idx = (int)i;
		}
	}
}

// ============================================================================
// CAMERA
// ============================================================================

void Systems::update_camera(Camera& camera, const Vec2& target_pos, bool player_jumping, float dt) {
	Vec2 diff = target_pos - camera.pos;
	camera.pos = camera.pos + diff * dt * CAMERA_FOLLOW_SPEED;
	float target_zoom = player_jumping ? ZOOM_JUMP : ZOOM_IDLE;
	camera.zoom += (target_zoom - camera.zoom) * dt * CAMERA_ZOOM_SPEED;
}
