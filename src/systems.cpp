#include "systems.hpp"
#include <SDL.h>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Constants ---
static constexpr float TIME_SCALE_MIN = 0.15f;
static constexpr float TIME_SCALE_MAX = 1.0f;
static constexpr float BASE_ENEMY_COUNT = 3.0f;
static constexpr float ENEMY_COUNT_SCALE = 2.5f;
static constexpr float SPAWN_DIST_MIN = 900.0f;
static constexpr float SPAWN_DIST_VAR = 300.0f;
static constexpr float MIN_SPAWN_SPEED = 8.0f;
static constexpr float MAX_SPAWN_SPEED = 16.0f;
static constexpr float MIN_ENEMY_RADIUS = 14.0f;
static constexpr float MAX_ENEMY_RADIUS = 24.0f;
static constexpr float SPAWN_TIMER_MIN = 0.3f;
static constexpr float SPAWN_TIMER_MAX = 0.8f;
static constexpr float MIN_ATTACK_DIST = 40.0f;
static constexpr float JUMP_DURATION = 0.25f;
static constexpr float CURVE_AMOUNT = 120.0f;
static constexpr float SWORD_LENGTH = 115.0f;
static constexpr float SWORD_HIT_RANGE = 70.0f;
static constexpr float BLOW_RADIUS = 300.0f;
static constexpr float BLOW_FORCE = 400.0f;
static constexpr float BLOW_DURATION = 0.6f;
static constexpr float FLASH_DURATION = 0.15f;
static constexpr float ENEMY_SPEED = 320.0f;
static constexpr float ENEMY_MAX_SPEED = 600.0f;
static constexpr float ENEMY_FEAR_SPEED = 700.0f;
static constexpr float PLAYER_DRIFT_SPEED = 180.0f;
static constexpr float PLAYER_INERTIA_FRICTION = 0.35f;
static constexpr float PLAYER_CURVE_RATE = 2.5f;
static constexpr float PLAYER_CURVE_AMP = 1.4f;
static constexpr float ZOOM_IDLE = 1.3f;
static constexpr float ZOOM_JUMP = 1.05f;
static constexpr float SCREEN_CENTER_X = 0.5f;
static constexpr float SCREEN_CENTER_Y = 0.45f;
static constexpr float TIMING_THRESHOLD = 0.55f;
static constexpr float TIMING_WINDOW_SEC = 0.18f;
static constexpr int BASE_HIT_POINTS = 100;
static constexpr int KILL_POINTS = 50;
static constexpr float FILL_PER_HIT = 0.15f;
static constexpr float MISS_PENALTY = 0.15f;
static constexpr float LEVEL_MULTIPLIER = 0.1f;
static constexpr int MAX_LEVEL = 7;

// --- Sword animation ---
static constexpr float SWORD_WINDUP_LEFT = M_PI * 0.92f;
static constexpr float SWORD_WINDUP_RIGHT = -M_PI * 0.92f;
static constexpr float SWORD_END_LEFT = M_PI * 0.75f;
static constexpr float SWORD_END_RIGHT = -M_PI * 0.75f;
static constexpr float SWORD_SLASH_SPEED = 28.0f;
static constexpr float SWORD_CHARGE_SPEED = 18.0f;
static constexpr float SWORD_IDLE_SPEED = 6.0f;
static constexpr float ANGLE_ROTATION_SPEED = 12.0f;
static constexpr float CAMERA_FOLLOW_SPEED = 8.0f;
static constexpr float CAMERA_ZOOM_SPEED = 10.0f;

// --- Attack phases ---
static constexpr float CHARGE_RATIO = 0.35f;
static constexpr float SLASH_RATIO = 0.35f;
static constexpr float RECOVER_RATIO = 0.30f;

// --- Ribbon constants ---
static constexpr float RIBBON_LIFETIME = 1.0f;
static constexpr int RIBBON_SEGMENTS_CHARGE = 3;
static constexpr int RIBBON_SEGMENTS_SLASH = 8;
static constexpr int RIBBON_SEGMENTS_IDLE = 1;

// --- Sword animation state ---
static struct SwordAnimState {
    bool slash_from_left = true;
    float last_end_offset = M_PI * 0.35f;
    float visual_offset = M_PI * 0.35f;
} g_sword;

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
    player.state_duration = JUMP_DURATION;
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
            sword_scale = 115.0f * sword_model.default_scale;
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
// COMBAT
// ============================================================================

static int find_nearest_enemy(const Player& player, const std::vector<Enemy>& enemies) {
    int nearest = -1;
    float min_dist = 999999.0f;
    for (size_t i = 0; i < enemies.size(); i++) {
        if (!enemies[i].alive) continue;
        float d = (enemies[i].pos - player.pos).len();
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

void Systems::update_combat(GameState& game, EventBus& events, float dt, const Timeline& timeline, float music_time) {
    Player& p = game.player;

    // Process attacks
    for (const auto& evt : events.attacks) {
        (void)evt;

        // Can't attack while charging or slashing unless chaining
        if (p.state == EntityState::Charging) continue;
        if (p.state == EntityState::Slashing && !p.can_chain) continue;

        // Score timing check
        bool good_timing = is_good_timing(timeline, music_time);

        p.state = EntityState::Charging;
        p.state_timer = JUMP_DURATION;
        p.state_duration = JUMP_DURATION;
        p.has_slashed = false;

        int target = find_nearest_enemy(p, game.enemies);
        if (target >= 0) {
            Vec2 enemy_pos = game.enemies[target].pos;
            p.target_enemy = target;
            p.jump_start = p.pos;
            Vec2 dir = (enemy_pos - p.pos).normalized();
            p.jump_target = enemy_pos - dir * (game.sword_scale * 0.66f);
            Vec2 mid = (p.jump_start + p.jump_target) * 0.5f;
            Vec2 perp(-dir.y, dir.x);
            float curve = (randf() > 0.5f ? 1.0f : -1.0f) * CURVE_AMOUNT;
            p.jump_control = mid + perp * curve;
            p.target_angle = atan2f(dir.y, dir.x);
        } else {
            p.target_enemy = -1;
            Vec2 dir(cosf(p.target_angle), sinf(p.target_angle));
            p.jump_start = p.pos;
            p.jump_target = p.pos + dir * 120.0f;
            p.jump_control = p.pos + dir * 60.0f;
        }

        events.scores.push_back({good_timing, target >= 0 ? 1 : 0});
    }

    // Update state timer
    if (p.state != EntityState::Idle) {
        p.state_timer -= dt;
        float progress = 1.0f - (p.state_timer / p.state_duration);
        progress = fmaxf(0.0f, fminf(1.0f, progress));

        if (p.state == EntityState::Charging && progress >= CHARGE_RATIO) {
            p.state = EntityState::Slashing;
        } else if (p.state == EntityState::Slashing && progress >= CHARGE_RATIO + SLASH_RATIO) {
            p.state = EntityState::Idle;
            p.pos = p.jump_target;
            p.target_enemy = -1;
            p.has_slashed = false;
        }
    }

    // Slash detection at ~50% through slash phase
    if (p.state == EntityState::Slashing && !p.has_slashed) {
        float slash_progress = (1.0f - (p.state_timer / p.state_duration) - CHARGE_RATIO) / SLASH_RATIO;
        if (slash_progress >= 0.5f) {
            p.has_slashed = true;

            // Compute exact sword pose at this instant for collision
            float windup = g_sword.slash_from_left ? SWORD_WINDUP_LEFT : SWORD_WINDUP_RIGHT;
            float end = g_sword.slash_from_left ? SWORD_END_RIGHT : SWORD_END_LEFT;
            float exact_offset = windup + (end - windup) * ease_out_cubic(slash_progress);
            float exact_pitch = -0.4f + 1.0f * ease_out_cubic(slash_progress);
            float sword_angle = p.angle + exact_offset;

            // Kill target enemy
            if (p.target_enemy >= 0 && p.target_enemy < (int)game.enemies.size() &&
                game.enemies[p.target_enemy].alive) {
                blow_away_enemies(game.enemies, game.enemies[p.target_enemy].pos);
                game.enemies[p.target_enemy].alive = false;
                game.enemies[p.target_enemy].flash_timer = FLASH_DURATION;
            }

            // Kill enemies intersecting sword collision polygon
            if (!game.sword_model.collision_poly.empty()) {
                Mat4 sword_mat = build_sword_matrix(p.pos, sword_angle, exact_pitch, game.sword_scale);
                Vec2 world_poly[4];
                for (int i = 0; i < 4; i++) {
                    Vec3 v = transform_point(sword_mat, Vec3(game.sword_model.collision_poly[i].x,
                                                              game.sword_model.collision_poly[i].y, 0.0f));
                    world_poly[i] = Vec2(v.x, v.y);
                }
                for (size_t i = 0; i < game.enemies.size(); i++) {
                    auto& en = game.enemies[i];
                    if (!en.alive || i == (size_t)p.target_enemy) continue;
                    float hit_dist = en.radius * 0.33f;
                    if (circle_intersects_poly(en.pos, hit_dist, world_poly, 4)) {
                        blow_away_enemies(game.enemies, en.pos);
                        en.alive = false;
                        en.flash_timer = FLASH_DURATION;
                    }
                }
            }
        }
    }

    // Can chain if target is dead
    bool target_dead = p.target_enemy < 0 || p.target_enemy >= (int)game.enemies.size() ||
                       !game.enemies[p.target_enemy].alive;
    p.can_chain = target_dead && p.state == EntityState::Slashing;

    // Update flash timers
    for (auto& en : game.enemies) {
        if (en.flash_timer > 0) en.flash_timer -= dt;
    }

    // Remove dead enemies
    game.enemies.erase(
        std::remove_if(game.enemies.begin(), game.enemies.end(),
            [](const Enemy& e) { return !e.alive && e.flash_timer <= 0 && e.blow_away_timer <= 0; }),
        game.enemies.end()
    );
}

// ============================================================================
// MOVEMENT
// ============================================================================

void Systems::update_movement(GameState& game, float dt) {
    Player& p = game.player;
    
    // Smooth rotation toward target angle (always active)
    float angle_diff_val = angle_diff(p.target_angle, p.angle);
    p.angle += angle_diff_val * dt * ANGLE_ROTATION_SPEED;
    
    // Player jump
    if (p.state != EntityState::Idle) {
        float progress = 1.0f - (p.state_timer / p.state_duration);
        progress = fmaxf(0.0f, fminf(1.0f, progress));
        
        float move_progress;
        if (progress < CHARGE_RATIO) {
            move_progress = ease_in_cubic(progress / CHARGE_RATIO) * 0.65f;
        } else if (progress < CHARGE_RATIO + SLASH_RATIO) {
            move_progress = 0.65f + 0.35f * ease_out_quad((progress - CHARGE_RATIO) / SLASH_RATIO);
        } else {
            move_progress = 1.0f;
        }
        
        Vec2 prev_pos = p.pos;
        p.pos = bezier(move_progress, p.jump_start, p.jump_control, p.jump_target);
        p.vel = (p.pos - prev_pos) * (1.0f / dt);  // derive velocity from movement
    } else {
        // Inertia drift when idle - curved motion, never fully stop
        p.pos = p.pos + p.vel * dt;
        p.vel = p.vel * (1.0f - dt * PLAYER_INERTIA_FRICTION);
        
        // Curved drift: direction rotates in a wave pattern
        static float drift_phase = 0.0f;
        drift_phase += dt * PLAYER_CURVE_RATE;
        float drift_angle = p.angle + sinf(drift_phase) * PLAYER_CURVE_AMP;
        Vec2 drift(cosf(drift_angle), sinf(drift_angle));
        p.vel = p.vel + drift * PLAYER_DRIFT_SPEED * dt;
        
        // Stronger random jitter for organic chaotic feel
        p.vel = p.vel + Vec2((randf() - 0.5f) * 35.0f, (randf() - 0.5f) * 35.0f) * dt;
    }
    
    // Enemy movement
    for (auto& en : game.enemies) {
        if (en.being_blown && en.blow_away_timer > 0) {
            en.blow_away_timer -= dt;
            en.pos = en.pos + en.blow_away_vel * dt;
            en.blow_away_vel = en.blow_away_vel * (1.0f - dt * 2.0f);
            if (en.blow_away_timer <= 0) {
                en.being_blown = false;
                en.fear_timer = 1.0f + randf() * 0.8f;
                en.behavior_timer = 0;
                // Keep some residual blow velocity for natural transition
                en.vel = en.blow_away_vel * 0.3f;
                en.blow_away_vel = Vec2(0, 0);
            }
        } else if (en.fear_timer > 0 && en.alive) {
            // Fear state: panic flee with high acceleration
            en.fear_timer -= dt;
            Vec2 away = (en.pos - p.pos).normalized();
            // Rapid direction jitter for chaotic panic
            float panic_jitter = (randf() - 0.5f) * M_PI * 1.2f;
            float cos_p = cosf(panic_jitter);
            float sin_p = sinf(panic_jitter);
            Vec2 panic_dir(away.x * cos_p - away.y * sin_p, away.x * sin_p + away.y * cos_p);
            // Strong acceleration away
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
            // Demonic behavior state machine
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
                // On state change, dampen old velocity for cleaner transitions
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
    // Ensure spawn is well outside camera view regardless of zoom/screen size
    // Conservative: assume min screen 800x600, but scale with zoom
    float min_visible = 400.0f / camera.zoom;  // half of small screen
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
            // Don't clamp here - let the level drop logic handle negative fill
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
    // Drop down on severe underfill (check BEFORE clamping to 0)
    if (s.fill < 0 && s.level > 0) {
        s.level--;
        s.fill = 0.75f + s.fill;  // carry over negative as penalty
        if (s.fill < 0) s.fill = 0;
        s.combo = 0;
    } else if (s.fill < 0) {
        s.fill = 0;
    }
    
    // Bar bounce animation from gradient
    s.bar_bounce += (gradient - s.bar_bounce) * dt * 10.0f;
}

// ============================================================================
// ANIMATION
// ============================================================================

void Systems::build_visual_frame(GameState& game, float dt, VisualFrame& out) {
    Player& p = game.player;

    // Compute phase
    EntityState phase = p.state;
    float phase_progress = 0.0f;
    if (p.state != EntityState::Idle) {
        float progress = 1.0f - (p.state_timer / p.state_duration);
        progress = fmaxf(0.0f, fminf(1.0f, progress));
        if (progress < CHARGE_RATIO) {
            phase = EntityState::Charging;
            phase_progress = progress / CHARGE_RATIO;
        } else if (progress < CHARGE_RATIO + SLASH_RATIO) {
            phase = EntityState::Slashing;
            phase_progress = (progress - CHARGE_RATIO) / SLASH_RATIO;
        } else {
            phase = EntityState::Idle;
            phase_progress = 1.0f;
        }
    }

    // Track direction
    if (p.state == EntityState::Charging) {
        g_sword.slash_from_left = angle_diff(g_sword.last_end_offset, 0.0f) > 0.0f;
    }
    if (phase == EntityState::Slashing) {
        g_sword.last_end_offset = g_sword.visual_offset;
    }

    // Compute target offset
    float windup = g_sword.slash_from_left ? SWORD_WINDUP_LEFT : SWORD_WINDUP_RIGHT;
    float end = g_sword.slash_from_left ? SWORD_END_RIGHT : SWORD_END_LEFT;

    float target_offset;
    switch (phase) {
        case EntityState::Idle: target_offset = g_sword.last_end_offset; break;
        case EntityState::Charging: target_offset = windup; break;
        case EntityState::Slashing: target_offset = windup + (end - windup) * ease_out_cubic(phase_progress); break;
    }

    // Smooth interpolate
    float diff = angle_diff(target_offset, g_sword.visual_offset);
    float speed = (phase == EntityState::Slashing) ? SWORD_SLASH_SPEED :
                  (phase == EntityState::Charging) ? SWORD_CHARGE_SPEED : SWORD_IDLE_SPEED;
    g_sword.visual_offset += diff * dt * speed;

    // Z-curve (pitch) animation
    float sword_pitch = 0.0f;
    switch (phase) {
        case EntityState::Idle: sword_pitch = 0.0f; break;
        case EntityState::Charging: sword_pitch = -0.4f * (1.0f - phase_progress); break;
        case EntityState::Slashing: sword_pitch = -0.4f + 1.0f * ease_out_cubic(phase_progress); break;
    }

    // Build frame
    out.player_pos = p.pos;
    out.player_angle = p.angle;
    out.sword_angle = p.angle + g_sword.visual_offset;
    out.player_state = p.state;
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
        Mat4 sword_mat = build_sword_matrix(p.pos, out.sword_angle, sword_pitch, game.sword_scale);
        out.sword_verts_world.reserve(game.sword_model.vertices.size());
        for (const auto& v : game.sword_model.vertices) {
            out.sword_verts_world.push_back(transform_point(sword_mat, v));
        }
        out.sword_tip_world = transform_point(sword_mat, game.sword_model.tip_vertex);
        out.has_sword_model = true;
    }

    // Update ribbons (persist in player)
    Vec2 sword_base = p.pos;
    Vec2 sword_tip(out.sword_tip_world.x, out.sword_tip_world.y);

    int segments;
    float ribbon_intensity;
    switch (phase) {
        case EntityState::Charging: segments = RIBBON_SEGMENTS_CHARGE; ribbon_intensity = 0.5f; break;
        case EntityState::Slashing: segments = RIBBON_SEGMENTS_SLASH; ribbon_intensity = 1.0f; break;
        default: segments = RIBBON_SEGMENTS_IDLE; ribbon_intensity = 0.25f; break;
    }

    for (int i = 0; i < segments; i++) {
        SwordRibbon r;
        r.base = sword_base;
        r.tip = sword_tip;
        r.lifetime = RIBBON_LIFETIME;
        r.max_lifetime = RIBBON_LIFETIME;
        r.intensity = ribbon_intensity;
        p.ribbons.push_back(r);
    }

    for (auto& sr : p.ribbons) sr.lifetime -= dt;
    p.ribbons.erase(
        std::remove_if(p.ribbons.begin(), p.ribbons.end(),
            [](const SwordRibbon& r) { return r.lifetime <= 0; }),
        p.ribbons.end()
    );

    out.ribbons = p.ribbons;

    // UI with smooth animation
    ScoreData& s = game.score;
    s.display_points += (s.points - s.display_points) * dt * 8.0f;
    s.display_fill += (s.fill - s.display_fill) * dt * 6.0f;

    if (s.level != s.display_level) {
        s.level_anim_timer += dt * 4.0f;
        if (s.level_anim_timer >= 1.0f) {
            s.display_level = s.level;
            s.level_anim_timer = 0.0f;
        }
    }

    out.score_fill = s.display_fill;
    out.score_level = s.display_level;
    out.score_feedback_timer = s.feedback_timer;
    out.score_feedback_good = s.last_hit_good;
    out.score_bar_bounce = s.bar_bounce;

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
