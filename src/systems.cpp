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
static constexpr float SPAWN_DIST = 350.0f;
static constexpr float SPAWN_DIST_VAR = 150.0f;
static constexpr float MIN_SPAWN_SPEED = 3.0f;
static constexpr float MAX_SPAWN_SPEED = 6.0f;
static constexpr float MIN_ENEMY_RADIUS = 14.0f;
static constexpr float MAX_ENEMY_RADIUS = 24.0f;
static constexpr float SPAWN_TIMER_MIN = 0.5f;
static constexpr float SPAWN_TIMER_MAX = 1.5f;
static constexpr float MIN_ATTACK_DIST = 40.0f;
static constexpr float JUMP_DURATION = 0.25f;
static constexpr float CURVE_AMOUNT = 80.0f;
static constexpr float SWORD_LENGTH = 115.0f;
static constexpr float SWORD_HIT_RANGE = 70.0f;
static constexpr float BLOW_RADIUS = 300.0f;
static constexpr float BLOW_FORCE = 400.0f;
static constexpr float BLOW_DURATION = 0.6f;
static constexpr float FLASH_DURATION = 0.15f;
static constexpr float ENEMY_SPEED = 100.0f;
static constexpr float ENEMY_MAX_SPEED = 250.0f;
static constexpr float ZOOM_IDLE = 1.3f;
static constexpr float ZOOM_JUMP = 1.15f;
static constexpr float SCREEN_CENTER_X = 0.5f;
static constexpr float SCREEN_CENTER_Y = 0.45f;
static constexpr float TIMING_THRESHOLD = 0.6f;
static constexpr int BASE_HIT_POINTS = 100;
static constexpr int KILL_POINTS = 50;
static constexpr float FILL_PER_HIT = 0.15f;
static constexpr float MISS_PENALTY = 0.15f;
static constexpr float LEVEL_MULTIPLIER = 0.1f;
static constexpr int MAX_LEVEL = 10;

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
static constexpr float CAMERA_ZOOM_SPEED = 6.0f;

// --- Attack phases ---
static constexpr float CHARGE_RATIO = 0.35f;
static constexpr float SLASH_RATIO = 0.35f;
static constexpr float RECOVER_RATIO = 0.30f;

// --- Ribbon constants ---
static constexpr float RIBBON_LIFETIME = 1.0f;
static constexpr int RIBBON_SEGMENTS_CHARGE = 3;
static constexpr int RIBBON_SEGMENTS_SLASH = 8;
static constexpr int RIBBON_SEGMENTS_IDLE = 1;

// ============================================================================
// HELPERS
// ============================================================================

float get_brightness_at_time(const Timeline& timeline, float time) {
    if (timeline.gradient.empty()) return 0.0f;
    int idx = (int)(time * timeline.fps);
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
    return get_brightness_at_time(timeline, music_time) >= TIMING_THRESHOLD;
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
            p.jump_target = enemy_pos - dir * (SWORD_LENGTH * 0.5f);
            Vec2 mid = (p.jump_start + p.jump_target) * 0.5f;
            Vec2 perp(-dir.y, dir.x);
            float curve = (randf() > 0.5f ? 1.0f : -1.0f) * CURVE_AMOUNT;
            p.jump_control = mid + perp * curve;
            p.angle = atan2f(dir.y, dir.x);
        } else {
            p.target_enemy = -1;
            Vec2 dir(cosf(p.angle), sinf(p.angle));
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
            
            // Kill target enemy
            if (p.target_enemy >= 0 && p.target_enemy < (int)game.enemies.size() &&
                game.enemies[p.target_enemy].alive) {
                blow_away_enemies(game.enemies, game.enemies[p.target_enemy].pos);
                game.enemies[p.target_enemy].alive = false;
                game.enemies[p.target_enemy].flash_timer = FLASH_DURATION;
            }
            
            // Kill enemies in sword range
            float sword_angle = p.angle + p.sword_offset;
            Vec2 sword_tip(p.pos.x + cosf(sword_angle) * SWORD_LENGTH,
                          p.pos.y + sinf(sword_angle) * SWORD_LENGTH);
            for (size_t i = 0; i < game.enemies.size(); i++) {
                auto& en = game.enemies[i];
                if (!en.alive || i == (size_t)p.target_enemy) continue;
                if ((en.pos - sword_tip).len() < SWORD_HIT_RANGE) {
                    blow_away_enemies(game.enemies, en.pos);
                    en.alive = false;
                    en.flash_timer = FLASH_DURATION;
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
        
        p.pos = bezier(move_progress, p.jump_start, p.jump_control, p.jump_target);
        
        if (p.target_enemy >= 0) {
            float target_angle = atan2f(p.jump_target.y - p.jump_start.y, p.jump_target.x - p.jump_start.x);
            p.angle += angle_diff(target_angle, p.angle) * dt * ANGLE_ROTATION_SPEED;
        }
    }
    
    // Enemy movement
    for (auto& en : game.enemies) {
        if (en.being_blown && en.blow_away_timer > 0) {
            en.blow_away_timer -= dt;
            en.pos = en.pos + en.blow_away_vel * dt;
            en.blow_away_vel = en.blow_away_vel * (1.0f - dt * 2.0f);
            if (en.blow_away_timer <= 0) {
                en.being_blown = false;
                en.blow_away_vel = Vec2(0, 0);
            }
        } else if (en.alive) {
            Vec2 to_player = (p.pos - en.pos).normalized();
            en.vel.x += to_player.x * ENEMY_SPEED * dt;
            en.vel.y += to_player.y * ENEMY_SPEED * dt;
            
            float speed = en.vel.len();
            float max_speed = ENEMY_MAX_SPEED * (0.5f + 0.5f * en.base_speed / MAX_SPAWN_SPEED);
            if (speed > max_speed) en.vel = en.vel.normalized() * max_speed;
            en.pos = en.pos + en.vel * dt;
        }
    }
}

// ============================================================================
// SPAWN
// ============================================================================

static void spawn_enemy(std::vector<Enemy>& enemies, const Vec2& player_pos, float gradient) {
    Enemy en;
    float angle = randf() * 2.0f * M_PI;
    float dist = SPAWN_DIST + randf() * SPAWN_DIST_VAR;
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
            spawn_enemy(game.enemies, game.player.pos, gradient);
        }
    }
    
    game.spawn_timer -= dt;
    if (alive_count < max_count && game.spawn_timer <= 0) {
        spawn_enemy(game.enemies, game.player.pos, gradient);
        game.spawn_timer = SPAWN_TIMER_MIN + randf() * (SPAWN_TIMER_MAX - SPAWN_TIMER_MIN);
    }
}

// ============================================================================
// SCORE
// ============================================================================

void Systems::update_score(GameState& game, EventBus& events, float dt) {
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
            float penalty;
            if (s.misses == 0) penalty = 0.8f;
            else if (s.misses == 1) penalty = 0.5f;
            else if (s.misses == 2) penalty = 0.25f;
            else penalty = 0.1f;
            s.points = (int)(s.points * penalty);
            s.fill -= MISS_PENALTY;
            if (s.fill < 0) s.fill = 0;
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
}

// ============================================================================
// ANIMATION
// ============================================================================

static struct SwordAnimState {
    bool slash_from_left = true;
    float last_end_offset = 0.0f;
    float visual_offset = 0.0f;
} g_sword;

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
    
    // Update ribbons (persist in player)
    float sword_angle = p.angle + g_sword.visual_offset;
    Vec2 sword_base = p.pos;
    Vec2 sword_tip(p.pos.x + cosf(sword_angle) * SWORD_LENGTH,
                   p.pos.y + sinf(sword_angle) * SWORD_LENGTH);
    
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
