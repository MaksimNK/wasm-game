#include "render.hpp"
#include "systems.hpp"
#include <SDL.h>
#include <GLES2/gl2.h>
#include <cmath>
#include <algorithm>
#include <string>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Render constants ---
static constexpr Uint8 BASE_ALPHA_MIN = 80;
static constexpr Uint8 BASE_ALPHA_RANGE = 175;

// --- Progress bar ---
static constexpr int BAR_W = 377;
static constexpr int BAR_H = 17;
static constexpr float BAR_Y_RATIO = 1.21f;
static constexpr float WINDOW_SECONDS = 0.77f;
static constexpr float PLAYHEAD_RATIO = 0.5f;
static constexpr float DISTORTION_FACTOR = 6.0f;
static constexpr float DISTORTION_DIVISOR = 7.0f;

// --- Alpha ---
static constexpr float ALPHA_LOW_THRESHOLD = 0.3f;
static constexpr float ALPHA_LOW_DIVISOR = 0.1f;
static constexpr float ALPHA_LOW_MAX = 15.0f;
static constexpr float ALPHA_HIGH_DIVISOR = 0.7f;
static constexpr float ALPHA_HIGH_MAX = 240.0f;
static constexpr float ALPHA_HIGH_OFFSET = 15.0f;

// --- Player ---
static constexpr float PLAYER_TRI_SIZE = 12.0f;
static constexpr float PLAYER_TRI_FRONT = 1.5f;
static constexpr float PLAYER_TRI_REAR_ANGLE = 2.5f;

// --- Sword ---
static constexpr float SWORD_PERSP = 0.3f;

// --- Circle ---
static constexpr int CIRCLE_SEGMENTS = 8;
static constexpr float CIRCLE_INNER_RATIO = 0.6f;

// --- Score bar ---
static constexpr int SCORE_BAR_H = 8;
static constexpr int SCORE_BAR_W = BAR_W / 2;

// --- Window ---
static constexpr Uint32 WINDOW_FLAGS = SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;

static constexpr int MAX_RIPPLES = 64;
static constexpr float RIPPLE_DURATION = 0.7f;
static constexpr float RIPPLE_STRENGTH = 21.0f;

struct DrawCmd {
    GLenum mode;
    GLint offset;
    GLsizei count;
};

struct Renderer {
    SDL_Window* window = nullptr;
    SDL_GLContext gl_context = nullptr;
    GLuint program = 0;
    GLint pos_loc = -1;
    GLint color_loc = -1;
    GLint proj_loc = -1;
    GLint brightness_loc = -1;
    GLint fisheye_loc = -1;
    GLint screen_center_loc = -1;
    GLint ripple_count_loc = -1;
    GLint ripple_data_loc[MAX_RIPPLES];
    GLint ripple_time_loc[MAX_RIPPLES];
    GLint ripple_strength_loc[MAX_RIPPLES];
    GLuint vbo = 0;
    std::vector<float> verts;
    std::vector<DrawCmd> cmds;
};

static std::string load_shader_file(const char* path) {
    std::string source;
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[SHADER] Failed to open: %s\n", path);
        return source;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    source.resize(sz);
    fread(&source[0], 1, sz, f);
    fclose(f);
    return source;
}

static GLuint compile_shader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, nullptr, info_log);
        fprintf(stderr, "[SHADER] Compile failed: %s\n", info_log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool init_gl(Renderer* r) {
    std::string vs_src = load_shader_file("static/shaders/vertex.glsl");
    std::string fs_src = load_shader_file("static/shaders/fragment.glsl");
    if (vs_src.empty() || fs_src.empty()) return false;
    
    GLuint vs = compile_shader(vs_src.c_str(), GL_VERTEX_SHADER);
    GLuint fs = compile_shader(fs_src.c_str(), GL_FRAGMENT_SHADER);
    if (!vs || !fs) return false;
    
    r->program = glCreateProgram();
    glAttachShader(r->program, vs);
    glAttachShader(r->program, fs);
    glLinkProgram(r->program);
    
    GLint success;
    glGetProgramiv(r->program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(r->program, 512, nullptr, info_log);
        fprintf(stderr, "[SHADER] Link failed: %s\n", info_log);
        glDeleteProgram(r->program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        r->program = 0;
        return false;
    }
    
    glDeleteShader(vs);
    glDeleteShader(fs);
    
    r->pos_loc = glGetAttribLocation(r->program, "a_pos");
    r->color_loc = glGetAttribLocation(r->program, "a_color");
    r->proj_loc = glGetUniformLocation(r->program, "u_projection");
    r->brightness_loc = glGetUniformLocation(r->program, "u_brightness");
    r->fisheye_loc = glGetUniformLocation(r->program, "u_fisheye");
    r->screen_center_loc = glGetUniformLocation(r->program, "u_screen_center");
    r->ripple_count_loc = glGetUniformLocation(r->program, "u_ripple_count");
    for (int i = 0; i < MAX_RIPPLES; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "u_ripples[%d]", i);
        r->ripple_data_loc[i] = glGetUniformLocation(r->program, buf);
        snprintf(buf, sizeof(buf), "u_ripple_time[%d]", i);
        r->ripple_time_loc[i] = glGetUniformLocation(r->program, buf);
        snprintf(buf, sizeof(buf), "u_ripple_strength[%d]", i);
        r->ripple_strength_loc[i] = glGetUniformLocation(r->program, buf);
    }
    glGenBuffers(1, &r->vbo);
    return true;
}

static void push_line(Renderer* r, float x1, float y1, float x2, float y2, 
                      float R, float G, float B, float A) {
    r->verts.push_back(x1); r->verts.push_back(y1);
    r->verts.push_back(R); r->verts.push_back(G); r->verts.push_back(B); r->verts.push_back(A);
    r->verts.push_back(x2); r->verts.push_back(y2);
    r->verts.push_back(R); r->verts.push_back(G); r->verts.push_back(B); r->verts.push_back(A);
    if (!r->cmds.empty() && r->cmds.back().mode == GL_LINES) {
        r->cmds.back().count += 2;
    } else {
        r->cmds.push_back({GL_LINES, (GLint)(r->verts.size() / 6 - 2), 2});
    }
}

static void push_tri(Renderer* r, float x1, float y1, float x2, float y2, float x3, float y3,
                     float R, float G, float B, float A) {
    r->verts.push_back(x1); r->verts.push_back(y1);
    r->verts.push_back(R); r->verts.push_back(G); r->verts.push_back(B); r->verts.push_back(A);
    r->verts.push_back(x2); r->verts.push_back(y2);
    r->verts.push_back(R); r->verts.push_back(G); r->verts.push_back(B); r->verts.push_back(A);
    r->verts.push_back(x3); r->verts.push_back(y3);
    r->verts.push_back(R); r->verts.push_back(G); r->verts.push_back(B); r->verts.push_back(A);
    if (!r->cmds.empty() && r->cmds.back().mode == GL_TRIANGLES) {
        r->cmds.back().count += 3;
    } else {
        r->cmds.push_back({GL_TRIANGLES, (GLint)(r->verts.size() / 6 - 3), 3});
    }
}

static void draw_triangle_outline(Renderer* r, float x1, float y1, float x2, float y2, float x3, float y3,
                                   float R, float G, float B, float A) {
    push_line(r, x1, y1, x2, y2, R, G, B, A);
    push_line(r, x2, y2, x3, y3, R, G, B, A);
    push_line(r, x3, y3, x1, y1, R, G, B, A);
}

static void draw_circle(Renderer* r, float cx, float cy, float radius, float R, float G, float B, float A) {
    float step = 2.0f * M_PI / CIRCLE_SEGMENTS;
    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        float a1 = i * step;
        float a2 = (i + 1) * step;
        push_line(r, cx + cosf(a1) * radius, cy + sinf(a1) * radius,
                  cx + cosf(a2) * radius, cy + sinf(a2) * radius, R, G, B, A);
    }
}

static void fill_circle(Renderer* r, float cx, float cy, float radius, float R, float G, float B, float A) {
    float step = 2.0f * M_PI / CIRCLE_SEGMENTS;
    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        float a1 = i * step;
        float a2 = (i + 1) * step;
        push_tri(r, cx, cy, 
                 cx + cosf(a1) * radius, cy + sinf(a1) * radius,
                 cx + cosf(a2) * radius, cy + sinf(a2) * radius, R, G, B, A);
    }
}

static void draw_player(Renderer* r, float cx, float cy, float angle, float R, float G, float B, float A) {
    float tri_r = PLAYER_TRI_SIZE;
    float fx = cx + cosf(angle) * tri_r * PLAYER_TRI_FRONT;
    float fy = cy + sinf(angle) * tri_r * PLAYER_TRI_FRONT;
    float lx = cx + cosf(angle + PLAYER_TRI_REAR_ANGLE) * tri_r;
    float ly = cy + sinf(angle + PLAYER_TRI_REAR_ANGLE) * tri_r;
    float rx = cx + cosf(angle - PLAYER_TRI_REAR_ANGLE) * tri_r;
    float ry = cy + sinf(angle - PLAYER_TRI_REAR_ANGLE) * tri_r;
    
    push_tri(r, fx, fy, lx, ly, rx, ry, R, G, B, A);
    draw_triangle_outline(r, fx, fy, lx, ly, rx, ry, R, G, B, A);
}

static void draw_sword_model(Renderer* r, const VisualFrame& frame, int screen_w, int screen_h,
                              float R, float G, float B, float A) {
    if (!frame.has_sword_model || frame.sword_verts_world.size() < 3) return;

    const float PERSP = 0.3f;

    for (size_t i = 0; i + 2 < frame.sword_verts_world.size(); i += 3) {
        const Vec3& v0 = frame.sword_verts_world[i];
        const Vec3& v1 = frame.sword_verts_world[i+1];
        const Vec3& v2 = frame.sword_verts_world[i+2];

        Vec2 s0 = world_to_screen(Vec2(v0.x, v0.y + v0.z * PERSP), frame.camera, screen_w, screen_h);
        Vec2 s1 = world_to_screen(Vec2(v1.x, v1.y + v1.z * PERSP), frame.camera, screen_w, screen_h);
        Vec2 s2 = world_to_screen(Vec2(v2.x, v2.y + v2.z * PERSP), frame.camera, screen_w, screen_h);

        push_tri(r, s0.x, s0.y, s1.x, s1.y, s2.x, s2.y, R, G, B, A);
        // Silhouette outline
        push_line(r, s0.x, s0.y, s1.x, s1.y, R, G, B, A);
        push_line(r, s1.x, s1.y, s2.x, s2.y, R, G, B, A);
        push_line(r, s2.x, s2.y, s0.x, s0.y, R, G, B, A);
    }
}

static void draw_grid(Renderer* r, const Camera& camera, int screen_w, int screen_h) {
    float spacing = 100.0f;
    float alpha = 0.07f;
    float left = camera.pos.x - screen_w;
    float right = camera.pos.x + screen_w;
    float top = camera.pos.y - screen_h;
    float bottom = camera.pos.y + screen_h;
    float start_x = floorf(left / spacing) * spacing;
    float start_y = floorf(top / spacing) * spacing;
    for (float x = start_x; x <= right; x += spacing) {
        Vec2 s1 = world_to_screen(Vec2(x, top), camera, screen_w, screen_h);
        Vec2 s2 = world_to_screen(Vec2(x, bottom), camera, screen_w, screen_h);
        push_line(r, s1.x, s1.y, s2.x, s2.y, 1.0f, 1.0f, 1.0f, alpha);
    }
    for (float y = start_y; y <= bottom; y += spacing) {
        Vec2 s1 = world_to_screen(Vec2(left, y), camera, screen_w, screen_h);
        Vec2 s2 = world_to_screen(Vec2(right, y), camera, screen_w, screen_h);
        push_line(r, s1.x, s1.y, s2.x, s2.y, 1.0f, 1.0f, 1.0f, alpha);
    }
}

static void draw_enemy(Renderer* r, float cx, float cy, float radius, float R, float G, float B, float A) {
    fill_circle(r, cx, cy, radius, R, G, B, A);
    draw_circle(r, cx, cy, radius * CIRCLE_INNER_RATIO, R, G, B, A * 0.5f);
}

static void draw_ribbon(Renderer* r, const Vec2& prev_base, const Vec2& prev_tip,
                        const Vec2& curr_base, const Vec2& curr_tip,
                        float R, float G, float B, float A) {
    push_tri(r, prev_base.x, prev_base.y, prev_tip.x, prev_tip.y, curr_tip.x, curr_tip.y, R, G, B, A);
    push_tri(r, prev_base.x, prev_base.y, curr_tip.x, curr_tip.y, curr_base.x, curr_base.y, R, G, B, A);
}


static Uint8 get_bar_alpha(float gradient) {
    if (gradient < ALPHA_LOW_THRESHOLD) {
        return (Uint8)(gradient / ALPHA_LOW_DIVISOR * ALPHA_LOW_MAX);
    }
    return (Uint8)(ALPHA_HIGH_OFFSET + (gradient - ALPHA_LOW_THRESHOLD) / ALPHA_HIGH_DIVISOR * ALPHA_HIGH_MAX);
}

static int eq_height(float intensity, int max_h) {
    // 0 intensity = 1px, 1.0 intensity = max_h
    int h = (int)(1 + intensity * (max_h - 1));
    if (h < 1) h = 1;
    if (h > max_h) h = max_h;
    return h;
}

static void draw_score_bar(Renderer* r, int screen_w, int screen_h, const VisualFrame& frame,
                           const Timeline& timeline, float time) {
    float ui_offset_x = (frame.player_pos.x - frame.camera.pos.x) * frame.camera.zoom * 0.35f;
    float ui_offset_y = (frame.player_pos.y - frame.camera.pos.y) * frame.camera.zoom * 0.22f;
    
    int bar_x = (int)((screen_w - SCORE_BAR_W) / 2 + ui_offset_x);
    int bar_y = (int)((screen_h - BAR_H) / BAR_Y_RATIO + BAR_H + 12 + ui_offset_y);
    int center_y = bar_y + SCORE_BAR_H / 2;
    int max_half_h = SCORE_BAR_H / 2;
    
    float fill_extent = frame.score_fill;
    float bounce = frame.score_bar_bounce;
    
    float rank_r, rank_g, rank_b;
    get_score_rank_color(frame.score_level, rank_r, rank_g, rank_b);
    
    int num_bars = 16;
    int bar_gap = 2;
    int bar_w = (SCORE_BAR_W - (num_bars - 1) * bar_gap) / num_bars;
    int total_w = num_bars * bar_w + (num_bars - 1) * bar_gap;
    int start_x = bar_x + (SCORE_BAR_W - total_w) / 2;
    
    for (int i = 0; i < num_bars; i++) {
        float rat = i / (float)(num_bars - 1);
        float dist_from_center = fabsf(rat - 0.5f) / 0.5f;
        
        if (dist_from_center > fill_extent) continue;
        
        float intensity = 1.0f - dist_from_center * 0.5f;
        
        // Sync each bar to music gradient
        float time_offset = (rat - 0.5f) * 0.5f;
        float sample_grad = get_brightness_at_time(timeline, time + time_offset);
        float bar_intensity = bounce * 0.3f + sample_grad * 0.7f;
        
        int half_h = eq_height(bar_intensity * intensity, max_half_h);
        
        int x = start_x + i * (bar_w + bar_gap);
        
        float r_val = rank_r * (1.0f + intensity * 0.3f);
        float g_val = rank_g * (1.0f + intensity * 0.3f);
        float b_val = rank_b * (1.0f + intensity * 0.3f);
        
        Uint8 alpha = get_bar_alpha(intensity);
        push_tri(r, x, center_y - half_h, x + bar_w, center_y - half_h, x + bar_w, center_y + half_h,
                 r_val, g_val, b_val, alpha / 255.0f);
        push_tri(r, x, center_y - half_h, x + bar_w, center_y + half_h, x, center_y + half_h,
                 r_val, g_val, b_val, alpha / 255.0f);
    }
    
    if (frame.score_feedback_timer > 0) {
        float alpha = frame.score_feedback_timer / 0.5f * 180.0f / 255.0f;
        float r_val = 255.0f / 255.0f;
        float g_val = frame.score_feedback_good ? 150.0f / 255.0f : 50.0f / 255.0f;
        float b_val = frame.score_feedback_good ? 150.0f / 255.0f : 50.0f / 255.0f;
        push_line(r, bar_x - 4, bar_y - 4, bar_x + SCORE_BAR_W + 4, bar_y - 4, r_val, g_val, b_val, alpha);
        push_line(r, bar_x - 4, bar_y + SCORE_BAR_H + 4, bar_x + SCORE_BAR_W + 4, bar_y + SCORE_BAR_H + 4, r_val, g_val, b_val, alpha);
        push_line(r, bar_x - 4, bar_y - 4, bar_x - 4, bar_y + SCORE_BAR_H + 4, r_val, g_val, b_val, alpha);
        push_line(r, bar_x + SCORE_BAR_W + 4, bar_y - 4, bar_x + SCORE_BAR_W + 4, bar_y + SCORE_BAR_H + 4, r_val, g_val, b_val, alpha);
    }
}

// --- Public API ---

Renderer* create_renderer(const char* title) {
    Renderer* r = new Renderer();
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    
    r->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  0, 0, WINDOW_FLAGS);
    if (!r->window) { delete r; return nullptr; }
    
    r->gl_context = SDL_GL_CreateContext(r->window);
    if (!r->gl_context) {
        fprintf(stderr, "[ERROR] GL context failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(r->window);
        delete r;
        return nullptr;
    }
    
    if (!init_gl(r)) {
        SDL_GL_DeleteContext(r->gl_context);
        SDL_DestroyWindow(r->window);
        delete r;
        return nullptr;
    }
    
    return r;
}

void destroy_renderer(Renderer* r) {
    if (!r) return;
    if (r->program) glDeleteProgram(r->program);
    if (r->vbo) glDeleteBuffers(1, &r->vbo);
    if (r->gl_context) SDL_GL_DeleteContext(r->gl_context);
    if (r->window) SDL_DestroyWindow(r->window);
    delete r;
}

bool is_renderer_valid(Renderer* r) {
    return r != nullptr && r->window != nullptr && r->gl_context != nullptr;
}

void get_screen_size(Renderer* r, int& w, int& h) {
    if (!r) { w = 800; h = 600; return; }
    SDL_GL_GetDrawableSize(r->window, &w, &h);
    if (w <= 0 || h <= 0) SDL_GetWindowSize(r->window, &w, &h);
    if (w <= 0 || h <= 0) { w = 800; h = 600; }
}

void render_frame(Renderer* r, const VisualFrame& frame, const Timeline& timeline,
                  float time, bool running) {
    if (!r || !r->window) return;
    
    int w, h;
    get_screen_size(r, w, h);
    
    float gradient = get_brightness_at_time(timeline, time);
    Uint8 base_alpha = (Uint8)(BASE_ALPHA_MIN + gradient * BASE_ALPHA_RANGE);
    float base_a = base_alpha / 255.0f;
    
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    float proj[16] = {
        2.0f/w, 0,       0, 0,
        0,      -2.0f/h, 0, 0,
        0,      0,       -1, 0,
        -1,     1,       0, 1
    };
    
    glUseProgram(r->program);
    glUniformMatrix4fv(r->proj_loc, 1, GL_FALSE, proj);
    
    // Shader effects scaled by music intensity
    float brightness = 0.65f + gradient * 0.55f;
    float fisheye = 0.5 + gradient / 5.0f;
    glUniform1f(r->brightness_loc, brightness);
    glUniform1f(r->fisheye_loc, fisheye);
    glUniform2f(r->screen_center_loc, w * 0.5f, h * 0.5f);
    
    // Upload ripple VFX data
    int ripple_count = (int)frame.ripples.size();
    if (ripple_count > MAX_RIPPLES) ripple_count = MAX_RIPPLES;
    glUniform1i(r->ripple_count_loc, ripple_count);
    for (int i = 0; i < ripple_count; i++) {
        const Ripple& rip = frame.ripples[i];
        Vec2 screen_pos = world_to_screen(rip.pos, frame.camera, w, h);
        Vec2 screen_dir = rip.dir * frame.camera.zoom;
        glUniform4f(r->ripple_data_loc[i], screen_pos.x, screen_pos.y, screen_dir.x, screen_dir.y);
        glUniform1f(r->ripple_time_loc[i], rip.timer);
        float strength = RIPPLE_STRENGTH * (1.0f - rip.timer / RIPPLE_DURATION);
        glUniform1f(r->ripple_strength_loc[i], strength);
    }
    
    r->verts.clear();
    r->cmds.clear();
    
    float sword_r, sword_g, sword_b;
    get_score_rank_color(frame.score_level, sword_r, sword_g, sword_b);
    
    if (running) {
        // Background grid
        draw_grid(r, frame.camera, w, h);
        
        // Ribbons (additive blending to fix overlap artifacts)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        for (size_t i = 1; i < frame.ribbons.size(); i++) {
            const auto& prev = frame.ribbons[i-1];
            const auto& curr = frame.ribbons[i];
            
            float life_ratio = (prev.lifetime + curr.lifetime) * 0.5f / prev.max_lifetime;
            if (life_ratio > 1.0f) life_ratio = 1.0f;
            if (life_ratio <= 0) continue;
            
            float ghost_alpha = fminf(1.0f, curr.intensity * life_ratio);
            
            Vec2 pb = world_to_screen(prev.base, frame.camera, w, h);
            Vec2 pt = world_to_screen(prev.tip, frame.camera, w, h);
            Vec2 cb = world_to_screen(curr.base, frame.camera, w, h);
            Vec2 ct = world_to_screen(curr.tip, frame.camera, w, h);
            
            draw_ribbon(r, pb, pt, cb, ct, sword_r, sword_g, sword_b, ghost_alpha);
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        // Enemies
        for (size_t i = 0; i < frame.enemies.size(); i++) {
            const auto& en = frame.enemies[i];
            Vec2 s = world_to_screen(en.pos, frame.camera, w, h);
            float rad = en.radius * frame.camera.zoom;
            draw_enemy(r, s.x, s.y, rad, 1, 1, 1, en.alpha);
            // Nearest enemy indicator
            if ((int)i == frame.nearest_enemy_idx) {
                draw_circle(r, s.x, s.y, rad * 1.3f, 1, 0.3f, 0.3f, 0.6f);
            }
        }
        
        // Player
        Vec2 ps = world_to_screen(frame.player_pos, frame.camera, w, h);
        draw_player(r, ps.x, ps.y, frame.player_angle, 1, 1, 1, base_a);
        
        // Sword model (colored by rank, no alpha modulation)
        draw_sword_model(r, frame, w, h, sword_r, sword_g, sword_b, 1.0f);
        
        // Score bar
        draw_score_bar(r, w, h, frame, timeline, time);
    }

    // Progress bar
    float ui_offset_x = (frame.player_pos.x - frame.camera.pos.x) * frame.camera.zoom * 0.35f;
    float ui_offset_y = (frame.player_pos.y - frame.camera.pos.y) * frame.camera.zoom * 0.22f;

    int bar_x = (int)((w - BAR_W) / 2 + ui_offset_x);
    int bar_y = (int)((h - BAR_H) / BAR_Y_RATIO + ui_offset_y);
    int center_y = bar_y + BAR_H / 2;

    if (!timeline.gradient.empty()) {
        // Inverse hyperbolic: center linear, edges compressed with hyperbolic falloff
        auto distort = [](float x) -> float {
            float abs_x = fabsf(x);
            float sign = x >= 0 ? 1.0f : -1.0f;
            float a = 0.85f;
            return sign * abs_x / (1.0f + a * abs_x);
        };
        
        int num_frames = timeline.gradient.size();
        for (int px = 0; px < BAR_W; px++) {
            float rat = px / (float)BAR_W;
            float d = rat - PLAYHEAD_RATIO;
            float max_d = d >= 0 ? (1.0f - PLAYHEAD_RATIO) : PLAYHEAD_RATIO;
            float normalized = d / max_d;
            float distorted = distort(normalized);
            float t = time + distorted * max_d * WINDOW_SECONDS;
            
            int frame_idx = (int)(t * timeline.sample_rate);
            if (frame_idx < 0 || frame_idx >= num_frames) continue;
            
            float g = timeline.gradient[frame_idx];
            int half_h = (int)(g * (BAR_H / 2));
            if (half_h < 1) half_h = 1;
            int x = bar_x + px;
            
            Uint8 alpha = get_bar_alpha(g);
            push_line(r, x, center_y - half_h, x, center_y + half_h, 
                      220.0f/255.0f, 60.0f/255.0f, 60.0f/255.0f, alpha / 255.0f);
        }
        
        // Check if playhead is in timing window for visual pulse
        bool in_timing_window = false;
        if (timeline.sample_rate > 0) {
            float window_sec = 0.18f;
            int window_frames = (int)(timeline.sample_rate * window_sec);
            int playhead_frame = (int)(time * timeline.sample_rate);
            if (playhead_frame >= 0 && playhead_frame < num_frames) {
                float local_peak = 0.0f;
                for (int f = playhead_frame - window_frames; f <= playhead_frame + window_frames; f++) {
                    if (f >= 0 && f < num_frames) {
                        local_peak = fmaxf(local_peak, timeline.gradient[f]);
                    }
                }
                in_timing_window = local_peak >= 0.55f && timeline.gradient[playhead_frame] >= local_peak * 0.85f;
            }
        }
        
        int playhead_x = bar_x + (int)(PLAYHEAD_RATIO * BAR_W);
        float ph_brightness = in_timing_window ? 1.6f : 1.0f;
        int ph_extend = in_timing_window ? 6 : 3;
        push_line(r, playhead_x, bar_y - ph_extend, playhead_x, bar_y + BAR_H + ph_extend, 
                  ph_brightness, ph_brightness * 0.4f, ph_brightness * 0.4f, 1.0f);
    }
    
    // Flush
    if (!r->verts.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
        glBufferData(GL_ARRAY_BUFFER, r->verts.size() * sizeof(float), r->verts.data(), GL_DYNAMIC_DRAW);
        
        for (const auto& cmd : r->cmds) {
            glVertexAttribPointer(r->pos_loc, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(r->pos_loc);
            glVertexAttribPointer(r->color_loc, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
            glEnableVertexAttribArray(r->color_loc);
            glDrawArrays(cmd.mode, cmd.offset, cmd.count);
        }
        
        glDisableVertexAttribArray(r->pos_loc);
        glDisableVertexAttribArray(r->color_loc);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    
    SDL_GL_SwapWindow(r->window);
}
