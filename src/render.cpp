#include "render.hpp"
#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif
#include <cmath>
#include <algorithm>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Render constants ---
static constexpr Uint8 BASE_ALPHA_MIN = 80;
static constexpr Uint8 BASE_ALPHA_RANGE = 175;
static constexpr float RIBBON_LIFETIME_BLEND = 0.5f;

// --- Progress bar constants ---
static constexpr int BAR_W = 377;
static constexpr int BAR_H = 17;
static constexpr float BAR_Y_RATIO = 1.21f;
static constexpr float WINDOW_SECONDS = 0.77f;
static constexpr float PLAYHEAD_RATIO = 0.21f;
static constexpr float DISTORTION_FACTOR = 6.0f;
static constexpr float DISTORTION_DIVISOR = 7.0f;
static constexpr float CENTER_LINE_ALPHA = 30;
static constexpr float PLAYHEAD_ALPHA = 255;

// --- Alpha thresholds ---
static constexpr float ALPHA_LOW_THRESHOLD = 0.3f;
static constexpr float ALPHA_LOW_DIVISOR = 0.1f;
static constexpr float ALPHA_LOW_MAX = 15.0f;
static constexpr float ALPHA_HIGH_DIVISOR = 0.7f;
static constexpr float ALPHA_HIGH_MAX = 240.0f;
static constexpr float ALPHA_HIGH_OFFSET = 15.0f;

// --- Animation constants ---
// Character faces enemy (angle=0). Back = behind character (π).
// Character's left = up (-π/2), right = down (+π/2).
static constexpr float SWORD_WINDUP_LEFT   =  M_PI * 0.92f;   // back-left (~166deg)
static constexpr float SWORD_WINDUP_RIGHT  = -M_PI * 0.92f;   // back-right (~-166deg)
static constexpr float SWORD_END_LEFT      =  M_PI * 0.75f;   // back-left follow-through
static constexpr float SWORD_END_RIGHT     = -M_PI * 0.75f;   // back-right follow-through
static constexpr float SWORD_IDLE_BASE_ANGLE = M_PI;          // directly behind
static constexpr float SWORD_IDLE_SWAY_AMP = 0.35f;
static constexpr float SWORD_IDLE_SWAY_FREQ = 2.0f;
static constexpr float SWORD_IDLE_SMOOTH_SPEED = 6.0f;
static constexpr float SWORD_CHARGE_SPEED = 18.0f;
static constexpr float SWORD_SLASH_SPEED = 28.0f;
static constexpr float ANGLE_ROTATION_SPEED = 12.0f;
static constexpr float CAMERA_FOLLOW_SPEED = 8.0f;
static constexpr float CAMERA_ZOOM_SPEED = 6.0f;
static constexpr float RIBBON_LIFETIME_IDLE = 0.6f;
static constexpr float RIBBON_LIFETIME_CHARGE = 0.7f;
static constexpr float RIBBON_LIFETIME_SLASH = 1.4f;
static constexpr float FLASH_DURATION = 0.15f;
static constexpr int RIBBON_SEGMENTS_IDLE = 3;
static constexpr int RIBBON_SEGMENTS_CHARGE = 5;
static constexpr int RIBBON_SEGMENTS_SLASH = 20;
static constexpr float SWORD_LENGTH = 115.0f;

// --- Player render constants ---
static constexpr float PLAYER_TRI_SIZE = 12.0f;
static constexpr float PLAYER_TRI_FRONT = 1.5f;
static constexpr float PLAYER_TRI_REAR_ANGLE = 2.5f;

// --- Sword render constants ---
static constexpr float SWORD_LEN = 115.0f;
static constexpr float SWORD_WIDTH = 5.0f;
static constexpr float SWORD_HANDLE_OFFSET = 5.0f;

// --- Circle constants ---
static constexpr int CIRCLE_SEGMENTS = 8;
static constexpr float CIRCLE_INNER_RATIO = 0.6f;

// --- Window constants ---
static constexpr Uint32 WINDOW_FLAGS_NATIVE = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
static constexpr Uint32 WINDOW_FLAGS_EMSCRIPTEN = SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;

// --- Forward declarations (score system additions at end of file) ---
static void updateScoreAnimations(ScoreSystem& sc, float dt);
static void drawScoreBar(SDL_Renderer* r, int screenW, int screenH, const GameState& game);

// --- Animation helpers ---

static float angleDiff(float a, float b) {
    float diff = a - b;
    while (diff > M_PI) diff -= 2.0f * M_PI;
    while (diff < -M_PI) diff += 2.0f * M_PI;
    return diff;
}

static float easeOutCubic(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

static float easeInCubic(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * t;
}

static float easeOutQuad(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    float u = 1.0f - t;
    return 1.0f - u * u;
}

static float easeInOutCubic(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    if (t < 0.5f) return 4.0f * t * t * t;
    float u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u / 2.0f;
}

static Vec2 bezier(float t, Vec2 p0, Vec2 p1, Vec2 p2) {
    float u = 1.0f - t;
    return p0 * (u * u) + p1 * (2.0f * u * t) + p2 * (t * t);
}

// --- Animation state (render-side) ---

enum class SwordPhase { IDLE, CHARGE, SLASH, RECOVER };

struct SwordAnimState {
    SwordPhase phase = SwordPhase::IDLE;
    float chargeStartOffset = 0.0f;
    float landTime = -999.0f;
    float lastAttackEndOffset = 0.0f;
    bool wasJumping = false;
    bool slashFromLeft = true;  // true = windup left, slash to right
};

static SwordAnimState g_swordAnim;

// --- Unified attack timeline ratios ---
static constexpr float CHARGE_RATIO  = 0.30f;
static constexpr float SLASH_RATIO   = 0.35f;
static constexpr float RECOVER_RATIO = 0.35f;

// --- Animation system (render team owns this) ---
// SINGLE unified timeline: attackProgress drives movement, sword, hit detection, ribbons

void updateAnimations(GameState& game, float dt) {
    // Detect jump start / land
    bool justStartedJump = game.player.jumping && !g_swordAnim.wasJumping;
    g_swordAnim.wasJumping = game.player.jumping;

    // --- Unified attack progress (0.0 = start, 1.0 = end) ---
    float attackProgress = 0.0f;
    if (game.player.jumping) {
        attackProgress = 1.0f - (game.player.jumpTimer / game.player.jumpDuration);
        attackProgress = fmaxf(0.0f, fminf(1.0f, attackProgress));
    }

    // --- Derive phase from unified progress ---
    if (game.player.jumping) {
        if (attackProgress < CHARGE_RATIO) {
            g_swordAnim.phase = SwordPhase::CHARGE;
        } else if (attackProgress < CHARGE_RATIO + SLASH_RATIO) {
            g_swordAnim.phase = SwordPhase::SLASH;
        } else {
            g_swordAnim.phase = SwordPhase::RECOVER;
        }
    } else {
        g_swordAnim.phase = SwordPhase::IDLE;
    }

    // --- Combo system: allow chaining during SLASH and RECOVER,
    //     but ONLY after current target enemy is dead ---
    bool targetIsDead = game.targetEnemy < 0 ||
                        game.targetEnemy >= (int)game.enemies.size() ||
                        !game.enemies[game.targetEnemy].alive;
    game.player.canChainAttack = targetIsDead &&
                                 ((g_swordAnim.phase == SwordPhase::SLASH) ||
                                  (g_swordAnim.phase == SwordPhase::RECOVER));

    // --- Direction init (once per jump or combo) ---
    if (justStartedJump || game.player.newAttack) {
        g_swordAnim.chargeStartOffset = g_swordAnim.lastAttackEndOffset;
        float normalized = angleDiff(g_swordAnim.lastAttackEndOffset, 0.0f);
        g_swordAnim.slashFromLeft = normalized > 0.0f;
        game.player.newAttack = false;
    }

    // Track sword end offset during chainable phases for smooth combos
    if (g_swordAnim.phase == SwordPhase::SLASH || g_swordAnim.phase == SwordPhase::RECOVER) {
        g_swordAnim.lastAttackEndOffset = game.player.swordOffset;
    }

    // Phase-local progress (0-1 within current phase)
    float phaseProgress = 0.0f;
    if (g_swordAnim.phase == SwordPhase::CHARGE) {
        phaseProgress = attackProgress / CHARGE_RATIO;
    } else if (g_swordAnim.phase == SwordPhase::SLASH) {
        phaseProgress = (attackProgress - CHARGE_RATIO) / SLASH_RATIO;
    } else if (g_swordAnim.phase == SwordPhase::RECOVER) {
        phaseProgress = (attackProgress - CHARGE_RATIO - SLASH_RATIO) / RECOVER_RATIO;
    }

    // --- Movement (driven by unified attackProgress) ---
    if (game.player.jumping) {
        float moveProgress;
        if (attackProgress < CHARGE_RATIO) {
            // Charge: fast leap to enemy (0% to 65%), aggressive acceleration
            moveProgress = easeInCubic(phaseProgress) * 0.65f;
        } else if (attackProgress < CHARGE_RATIO + SLASH_RATIO) {
            // Slash: close remaining distance (65% to 100%), contact
            moveProgress = 0.65f + 0.35f * easeOutQuad(phaseProgress);
        } else {
            // Recover: stay at destination
            moveProgress = 1.0f;
        }
        game.player.pos = bezier(moveProgress,
                                  game.player.jumpStart,
                                  game.player.jumpControl,
                                  game.player.jumpTarget);
    }

    // --- Smooth player angle rotation FIRST (before hit detection) ---
    if (game.player.jumping) {
        float ad = angleDiff(game.player.targetAngle, game.player.angle);
        game.player.angle += ad * dt * ANGLE_ROTATION_SPEED;
    }

    // --- Sword offset (driven by unified attackProgress) ---
    float windupOffset = g_swordAnim.slashFromLeft ? SWORD_WINDUP_LEFT : SWORD_WINDUP_RIGHT;
    float endOffset = g_swordAnim.slashFromLeft ? SWORD_END_RIGHT : SWORD_END_LEFT;

    float targetSwordOffset;
    if (g_swordAnim.phase == SwordPhase::IDLE) {
        targetSwordOffset = g_swordAnim.lastAttackEndOffset;
    } else if (g_swordAnim.phase == SwordPhase::CHARGE) {
        // Charge: sword held at windup position on character's back
        // No arc — just ensure it's at the back; smooth speed handles transition
        targetSwordOffset = windupOffset;
    } else if (g_swordAnim.phase == SwordPhase::SLASH) {
        float t = easeOutCubic(phaseProgress);
        targetSwordOffset = windupOffset + (endOffset - windupOffset) * t;
    } else { // RECOVER - hold at end position, no return animation
        targetSwordOffset = endOffset;
    }

    // Smooth interpolation to target
    float diff = angleDiff(targetSwordOffset, game.player.swordOffset);
    float speed;
    if (g_swordAnim.phase == SwordPhase::SLASH) {
        speed = SWORD_SLASH_SPEED;
    } else if (g_swordAnim.phase == SwordPhase::CHARGE) {
        speed = SWORD_CHARGE_SPEED;
    } else {
        speed = SWORD_IDLE_SMOOTH_SPEED;
    }
    game.player.swordOffset += diff * dt * speed;

    // --- Hit detection (synced to unified timeline) ---
    // Now runs AFTER angle/sword are updated for this frame
    if (g_swordAnim.phase == SwordPhase::SLASH && !game.player.hasSlashed) {
        if (phaseProgress >= 0.45f) {
            game.player.hasSlashed = true;
        }
    }

    // Camera smoothing
    Vec2 diffCam = game.player.pos - game.camera.pos;
    game.camera.pos = game.camera.pos + diffCam * dt * CAMERA_FOLLOW_SPEED;
    float targetZoom = game.player.jumping ? 1.05f : 1.45f;
    game.camera.zoom += (targetZoom - game.camera.zoom) * dt * CAMERA_ZOOM_SPEED;

    // --- Ribbon trail generation (phase-aware, driven by unified timeline) ---
    float swordAngle = game.player.angle + game.player.swordOffset;
    Vec2 swordBase = game.player.pos;
    Vec2 swordTip(
        game.player.pos.x + cosf(swordAngle) * SWORD_LENGTH,
        game.player.pos.y + sinf(swordAngle) * SWORD_LENGTH
    );

    int segments;
    float ribbonLifetime;
    float ribbonIntensity;

    switch (g_swordAnim.phase) {
        case SwordPhase::CHARGE:
            segments = RIBBON_SEGMENTS_CHARGE;
            ribbonLifetime = RIBBON_LIFETIME_CHARGE;
            ribbonIntensity = 0.5f;
            break;
        case SwordPhase::SLASH:
            segments = RIBBON_SEGMENTS_SLASH;
            ribbonLifetime = RIBBON_LIFETIME_SLASH;
            ribbonIntensity = 1.0f;
            break;
        case SwordPhase::RECOVER:
            segments = 0;
            ribbonLifetime = 0.0f;
            ribbonIntensity = 0.0f;
            break;
        default:
            segments = RIBBON_SEGMENTS_IDLE;
            ribbonLifetime = RIBBON_LIFETIME_IDLE;
            ribbonIntensity = 0.25f;
            break;
    }

    // Scale trail segments by music intensity (timeScale 0.15-1.0)
    float intensity = (game.timeScale - 0.15f) / 0.85f;
    float trailMultiplier = 0.3f + 0.7f * intensity; // 0.3 to 1.0
    segments = (int)(segments * trailMultiplier);

    for (int i = 0; i < segments; i++) {
        SwordRibbon ribbon;
        ribbon.base = swordBase;
        ribbon.tip = swordTip;
        ribbon.lifetime = ribbonLifetime;
        ribbon.maxLifetime = ribbonLifetime;
        ribbon.gradient = ribbonIntensity;
        game.player.swordRibbons.push_back(ribbon);
    }

    // Ribbon fade
    for (auto& sr : game.player.swordRibbons) sr.lifetime -= dt;
    game.player.swordRibbons.erase(
        std::remove_if(game.player.swordRibbons.begin(), game.player.swordRibbons.end(),
            [](const SwordRibbon& r) { return r.lifetime <= 0; }),
        game.player.swordRibbons.end()
    );

    // Hit feedback timer
    if (game.score.hitFeedbackTimer > 0) {
        game.score.hitFeedbackTimer -= dt;
    }

    // Score display smooth animation
    updateScoreAnimations(game.score, dt);
}

// --- GLSL Brightness Shader ---
#ifdef __EMSCRIPTEN__
static const char* brightnessVertexShader = R"(
attribute vec2 aPos;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char* brightnessFragmentShader = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uTexture;
uniform float uBrightness;
void main() {
    vec4 color = texture2D(uTexture, vTexCoord);
    gl_FragColor = color * uBrightness;
}
)";
#else
static const char* brightnessVertexShader = R"(
#version 120
attribute vec2 aPos;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char* brightnessFragmentShader = R"(
#version 120
varying vec2 vTexCoord;
uniform sampler2D uTexture;
uniform float uBrightness;
void main() {
    vec4 color = texture2D(uTexture, vTexCoord);
    gl_FragColor = color * uBrightness;
}
)";
#endif

static float quadVertices[] = {
    // positions    // texCoords
    -1.0f,  1.0f,  0.0f, 0.0f,
    -1.0f, -1.0f,  0.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 1.0f,
     1.0f,  1.0f,  1.0f, 0.0f
};

struct ShaderState {
    GLuint program = 0;
    GLuint quadVBO = 0;
    GLint brightnessLoc = -1;
    GLint posLoc = -1;
    GLint texCoordLoc = -1;
    bool initialized = false;
};

static GLuint compileShader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        fprintf(stderr, "[SHADER] Compilation failed: %s\n", infoLog);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool initShader(ShaderState& shader) {
    GLuint vs = compileShader(brightnessVertexShader, GL_VERTEX_SHADER);
    GLuint fs = compileShader(brightnessFragmentShader, GL_FRAGMENT_SHADER);
    if (!vs || !fs) return false;

    shader.program = glCreateProgram();
    glAttachShader(shader.program, vs);
    glAttachShader(shader.program, fs);
    glLinkProgram(shader.program);

    GLint success;
    glGetProgramiv(shader.program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(shader.program, 512, nullptr, infoLog);
        fprintf(stderr, "[SHADER] Linking failed: %s\n", infoLog);
        glDeleteProgram(shader.program);
        shader.program = 0;
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    shader.posLoc = glGetAttribLocation(shader.program, "aPos");
    shader.texCoordLoc = glGetAttribLocation(shader.program, "aTexCoord");
    shader.brightnessLoc = glGetUniformLocation(shader.program, "uBrightness");

    glGenBuffers(1, &shader.quadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, shader.quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    shader.initialized = true;
    return true;
}

static void destroyShader(ShaderState& shader) {
    if (shader.quadVBO) glDeleteBuffers(1, &shader.quadVBO);
    if (shader.program) glDeleteProgram(shader.program);
    shader = ShaderState();
}

// --- Renderer implementation ---
struct Renderer {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* postProcess = nullptr;
    GLuint screenTexture = 0;
    int screenTextureW = 0;
    int screenTextureH = 0;
    ShaderState shader;
    int width = 800;
    int height = 600;
};

static bool isOpenGLBackend(SDL_Renderer* renderer) {
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer, &info) < 0) return false;
    return strcmp(info.name, "opengl") == 0;
}

static void ensureScreenTexture(Renderer* r, int w, int h) {
    if (!r) return;
    if (r->screenTexture && r->screenTextureW == w && r->screenTextureH == h) return;
    if (r->screenTexture) glDeleteTextures(1, &r->screenTexture);
    glGenTextures(1, &r->screenTexture);
    glBindTexture(GL_TEXTURE_2D, r->screenTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    r->screenTextureW = w;
    r->screenTextureH = h;
}

// --- Internal drawing helpers ---

static void setColor(SDL_Renderer* r, Uint8 alpha) {
    SDL_SetRenderDrawColor(r, 255, 255, 255, alpha);
}

static void drawTriangle(SDL_Renderer* r, float x1, float y1, float x2, float y2, float x3, float y3) {
    SDL_RenderDrawLine(r, (int)x1, (int)y1, (int)x2, (int)y2);
    SDL_RenderDrawLine(r, (int)x2, (int)y2, (int)x3, (int)y3);
    SDL_RenderDrawLine(r, (int)x3, (int)y3, (int)x1, (int)y1);
}

static void fillTriangle(SDL_Renderer* r, float x1, float y1, float x2, float y2, float x3, float y3) {
    if (y1 > y2) { std::swap(x1, x2); std::swap(y1, y2); }
    if (y2 > y3) { std::swap(x2, x3); std::swap(y2, y3); }
    if (y1 > y2) { std::swap(x1, x2); std::swap(y1, y2); }
    
    float dy = y3 - y1;
    if (dy < 0.5f) return;
    
    for (int y = (int)y1; y <= (int)y3; y++) {
        float t = (y - y1) / dy;
        float xLeft, xRight;
        
        if (y < y2) {
            float t1 = (y2 - y1) > 0.01f ? (y - y1) / (y2 - y1) : 0;
            xLeft = x1 + (x2 - x1) * t1;
            xRight = x1 + (x3 - x1) * t;
        } else {
            float t2 = (y3 - y2) > 0.01f ? (y - y2) / (y3 - y2) : 0;
            xLeft = x2 + (x3 - x2) * t2;
            xRight = x1 + (x3 - x1) * t;
        }
        
        if (xLeft > xRight) std::swap(xLeft, xRight);
        SDL_RenderDrawLine(r, (int)xLeft, y, (int)xRight, y);
    }
}

static void drawCircle(SDL_Renderer* r, float cx, float cy, float radius) {
    float step = 2.0f * M_PI / CIRCLE_SEGMENTS;
    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        float a1 = i * step;
        float a2 = (i + 1) * step;
        SDL_RenderDrawLine(r,
            (int)(cx + cosf(a1) * radius), (int)(cy + sinf(a1) * radius),
            (int)(cx + cosf(a2) * radius), (int)(cy + sinf(a2) * radius));
    }
}

static void fillCircle(SDL_Renderer* r, float cx, float cy, float radius) {
    int rad = (int)(radius + 0.5f);
    for (int y = -rad; y <= rad; y++) {
        float dy = (float)y;
        float dx = sqrtf(std::max(0.0f, radius * radius - dy * dy));
        if (dx > 0.5f) {
            SDL_RenderDrawLine(r, (int)(cx - dx), (int)(cy + y), (int)(cx + dx), (int)(cy + y));
        }
    }
}

static void drawPlayer(SDL_Renderer* r, float cx, float cy, float angle, Uint8 alpha) {
    float triR = PLAYER_TRI_SIZE;
    float fx = cx + cosf(angle) * triR * PLAYER_TRI_FRONT;
    float fy = cy + sinf(angle) * triR * PLAYER_TRI_FRONT;
    float lx = cx + cosf(angle + PLAYER_TRI_REAR_ANGLE) * triR;
    float ly = cy + sinf(angle + PLAYER_TRI_REAR_ANGLE) * triR;
    float rx = cx + cosf(angle - PLAYER_TRI_REAR_ANGLE) * triR;
    float ry = cy + sinf(angle - PLAYER_TRI_REAR_ANGLE) * triR;
    
    setColor(r, alpha);
    fillTriangle(r, fx, fy, lx, ly, rx, ry);
    drawTriangle(r, fx, fy, lx, ly, rx, ry);
}

static void drawSword(SDL_Renderer* r, float cx, float cy, float angle, Uint8 alpha) {
    float hx = cosf(angle) * SWORD_HANDLE_OFFSET;
    float hy = sinf(angle) * SWORD_HANDLE_OFFSET;
    float tx = cosf(angle) * SWORD_LEN;
    float ty = sinf(angle) * SWORD_LEN;
    
    Vec2 base(cx + hx, cy + hy);
    Vec2 tip(cx + tx, cy + ty);
    Vec2 dir = Vec2(tip.x - base.x, tip.y - base.y).normalized();
    Vec2 perp(-dir.y, dir.x);
    
    float w = SWORD_WIDTH;
    float x1 = base.x + perp.x * w;
    float y1 = base.y + perp.y * w;
    float x2 = base.x - perp.x * w;
    float y2 = base.y - perp.y * w;
    
    setColor(r, alpha);
    fillTriangle(r, x1, y1, x2, y2, tip.x, tip.y);
    drawTriangle(r, x1, y1, x2, y2, tip.x, tip.y);
}

static void drawEnemy(SDL_Renderer* r, float cx, float cy, float radius, Uint8 alpha) {
    setColor(r, alpha);
    fillCircle(r, cx, cy, radius);
    setColor(r, (Uint8)(alpha * 0.5f));
    drawCircle(r, cx, cy, radius * CIRCLE_INNER_RATIO);
}

static void renderRibbon(SDL_Renderer* r, const Vec2& prevBase, const Vec2& prevTip,
                         const Vec2& currBase, const Vec2& currTip, Uint8 alpha) {
    float minY = std::min(std::min(prevBase.y, prevTip.y), std::min(currBase.y, currTip.y));
    float maxY = std::max(std::max(prevBase.y, prevTip.y), std::max(currBase.y, currTip.y));
    
    auto intersectSegment = [](float y, float ax, float ay, float bx, float by) -> float {
        if ((ay <= y && by > y) || (by <= y && ay > y)) {
            return ax + (y - ay) / (by - ay) * (bx - ax);
        }
        return 1e9f;
    };
    
    for (int py = (int)minY; py <= (int)maxY; py++) {
        float fy = (float)py;
        float xMin = 1e9f, xMax = -1e9f;
        
        float xs[4];
        xs[0] = intersectSegment(fy, prevBase.x, prevBase.y, prevTip.x, prevTip.y);
        xs[1] = intersectSegment(fy, prevTip.x, prevTip.y, currTip.x, currTip.y);
        xs[2] = intersectSegment(fy, currTip.x, currTip.y, currBase.x, currBase.y);
        xs[3] = intersectSegment(fy, currBase.x, currBase.y, prevBase.x, prevBase.y);
        
        for (int i = 0; i < 4; i++) {
            if (xs[i] < 1e8f) {
                xMin = std::min(xMin, xs[i]);
                xMax = std::max(xMax, xs[i]);
            }
        }
        
        if (xMax > xMin) {
            SDL_RenderDrawLine(r, (int)xMin, py, (int)xMax, py);
        }
    }
}

static Uint8 getBarAlpha(float gradient) {
    if (gradient < ALPHA_LOW_THRESHOLD) {
        return (Uint8)(gradient / ALPHA_LOW_DIVISOR * ALPHA_LOW_MAX);
    }
    return (Uint8)(ALPHA_HIGH_OFFSET + (gradient - ALPHA_LOW_THRESHOLD) / ALPHA_HIGH_DIVISOR * ALPHA_HIGH_MAX);
}

// --- Public API ---

static void ensurePostProcessTexture(Renderer* r, int w, int h) {
    if (!r || !r->renderer) return;
    if (r->postProcess) {
        int tw, th;
        SDL_QueryTexture(r->postProcess, nullptr, nullptr, &tw, &th);
        if (tw == w && th == h) return;
        SDL_DestroyTexture(r->postProcess);
        r->postProcess = nullptr;
    }
    r->postProcess = SDL_CreateTexture(r->renderer, SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_SetTextureBlendMode(r->postProcess, SDL_BLENDMODE_BLEND);
}

Renderer* createRenderer(const char* title, int width, int height) {
    Renderer* r = new Renderer();
    r->width = width;
    r->height = height;
    
#ifdef __EMSCRIPTEN__
    r->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  0, 0, WINDOW_FLAGS_EMSCRIPTEN);
#else
    r->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  width, height, WINDOW_FLAGS_NATIVE);
#endif
    if (!r->window) { delete r; return nullptr; }
    
    r->renderer = SDL_CreateRenderer(r->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r->renderer) {
        r->renderer = SDL_CreateRenderer(r->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!r->renderer) { SDL_DestroyWindow(r->window); delete r; return nullptr; }

    // Initialize GLSL shader using SDL's existing GL context (if OpenGL backend)
    if (isOpenGLBackend(r->renderer)) {
        initShader(r->shader);
    }

    return r;
}

void destroyRenderer(Renderer* r) {
    if (!r) return;
    destroyShader(r->shader);
    if (r->screenTexture) glDeleteTextures(1, &r->screenTexture);
    if (r->postProcess) SDL_DestroyTexture(r->postProcess);
    if (r->renderer) SDL_DestroyRenderer(r->renderer);
    if (r->window) SDL_DestroyWindow(r->window);
    delete r;
}

bool isRendererValid(Renderer* r) {
    return r != nullptr && r->renderer != nullptr;
}

void getScreenSize(Renderer* r, int& w, int& h) {
    if (!r) { w = 800; h = 600; return; }
    SDL_GetRendererOutputSize(r->renderer, &w, &h);
    if (w <= 0 || h <= 0) SDL_GetWindowSize(r->window, &w, &h);
    if (w <= 0 || h <= 0) { w = r->width; h = r->height; }
}

void renderFrame(Renderer* r, const GameState& game, const Timeline& timeline,
                 float time, bool running) {
    if (!r || !r->renderer) return;

    int w, h;
    getScreenSize(r, w, h);

    float gradient = getBrightnessAtTime(timeline, time);
    Uint8 baseAlpha = (Uint8)(BASE_ALPHA_MIN + gradient * BASE_ALPHA_RANGE);
    bool useGL = r->shader.initialized && isOpenGLBackend(r->renderer);

    // --- Setup render target ---
    if (useGL) {
        // Render directly to screen for GL framebuffer capture
        SDL_SetRenderTarget(r->renderer, nullptr);
    } else {
        // Fallback: render to texture for SDL color mod
        ensurePostProcessTexture(r, w, h);
        SDL_SetRenderTarget(r->renderer, r->postProcess);
    }

    SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 255);
    SDL_RenderClear(r->renderer);

    if (running) {
        SDL_SetRenderDrawBlendMode(r->renderer, SDL_BLENDMODE_BLEND);
        
        // Sword ribbon trail with interpolation for smooth curves
        for (size_t i = 1; i < game.player.swordRibbons.size(); i++) {
            const auto& prev = game.player.swordRibbons[i-1];
            const auto& curr = game.player.swordRibbons[i];
            
            float lifeRatio = (prev.lifetime + curr.lifetime) * RIBBON_LIFETIME_BLEND / prev.maxLifetime;
            if (lifeRatio > 1.0f) lifeRatio = 1.0f;
            if (lifeRatio <= 0) continue;
            
            Uint8 ghostAlpha = (Uint8)(fminf(255.0f, curr.gradient * 255.0f * lifeRatio / 1.35f));
            
            Vec2 prevBase = getWorldToScreen(game, prev.base, w, h);
            Vec2 prevTip = getWorldToScreen(game, prev.tip, w, h);
            Vec2 currBase = getWorldToScreen(game, curr.base, w, h);
            Vec2 currTip = getWorldToScreen(game, curr.tip, w, h);
            
            // Interpolate between stored ribbon positions for smooth curves
            // More steps for high-intensity phases (slash)
            int interpSteps = (curr.gradient > 0.8f) ? 2 : 1;
            Vec2 lastBase = prevBase;
            Vec2 lastTip = prevTip;
            
            for (int step = 1; step <= interpSteps; step++) {
                float t = step / (float)interpSteps;
                float smoothT = t * t * (3.0f - 2.0f * t); // smoothstep
                
                Vec2 ibase = prevBase + (currBase - prevBase) * smoothT;
                Vec2 itip = prevTip + (currTip - prevTip) * smoothT;
                
                setColor(r->renderer, ghostAlpha);
                renderRibbon(r->renderer, lastBase, lastTip, ibase, itip, ghostAlpha);
                
                lastBase = ibase;
                lastTip = itip;
            }
        }
        
        // Enemies
        for (const auto& e : game.enemies) {
            if (!e.alive && e.flashTimer <= 0 && e.blowAwayTimer <= 0) continue;
            Vec2 s = getWorldToScreen(game, e.pos, w, h);
            float rad = e.radius * game.camera.zoom;
            Uint8 a;
            if (!e.alive) {
                if (e.flashTimer > 0) {
                    float flashRatio = e.flashTimer / FLASH_DURATION;
                    a = (Uint8)(255 * flashRatio);
                } else {
                    a = (Uint8)(baseAlpha * 0.5f);
                }
            } else {
                a = e.flashTimer > 0 ? 255 : (e.beingBlown ? 255 : baseAlpha);
            }
            drawEnemy(r->renderer, s.x, s.y, rad, a);
        }
        
        // Player
        Vec2 ps = getWorldToScreen(game, game.player.pos, w, h);
        drawPlayer(r->renderer, ps.x, ps.y, game.player.angle, baseAlpha);
        
        // Sword (phase-aware alpha for visual punch)
        Uint8 swordAlpha;
        switch (g_swordAnim.phase) {
            case SwordPhase::SLASH:
                swordAlpha = 255;
                break;
            case SwordPhase::CHARGE:
                swordAlpha = (Uint8)(baseAlpha * 0.7f + 80);
                break;
            case SwordPhase::RECOVER:
                swordAlpha = (Uint8)(baseAlpha * 0.8f + 40);
                break;
            default:
                swordAlpha = (Uint8)(baseAlpha * 0.85f);
                break;
        }
        drawSword(r->renderer, ps.x, ps.y, game.player.angle + game.player.swordOffset, swordAlpha);
        
        // Score bar
        drawScoreBar(r->renderer, w, h, game);
    }
    
    // Dynamic UI offset based on character movement (more dramatic parallax)
    float uiOffsetX = (game.player.pos.x - game.camera.pos.x) * game.camera.zoom * 0.35f;
    float uiOffsetY = (game.player.pos.y - game.camera.pos.y) * game.camera.zoom * 0.22f;
    
    // Progress bar (always rendered)
    SDL_SetRenderDrawBlendMode(r->renderer, SDL_BLENDMODE_BLEND);
    
    int barX = (int)((w - BAR_W) / 2 + uiOffsetX);
    int barY = (int)((h - BAR_H) / BAR_Y_RATIO + uiOffsetY);
    int centerY = barY + BAR_H / 2;
    
    if (!timeline.gradient.empty()) {
        auto distort = [](float x) -> float {
            float absX = fabsf(x);
            float sign = x >= 0 ? 1.0f : -1.0f;
            return sign * absX * (1.0f + DISTORTION_FACTOR * absX * absX) / DISTORTION_DIVISOR;
        };
        
        int numFrames = timeline.gradient.size();
        for (int px = 0; px < BAR_W; px++) {
            float rat = px / (float)BAR_W;
            float d = rat - PLAYHEAD_RATIO;
            float maxD = d >= 0 ? (1.0f - PLAYHEAD_RATIO) : PLAYHEAD_RATIO;
            float normalized = d / maxD;
            float distorted = distort(normalized);
            float t = time + distorted * maxD * WINDOW_SECONDS;
            
            int frameIdx = (int)(t * timeline.fps);
            if (frameIdx < 0 || frameIdx >= numFrames) continue;
            
            float g = timeline.gradient[frameIdx];
            int halfH = (int)(g * (BAR_H / 2));
            if (halfH < 1) halfH = 1;
            int x = barX + px;
            
            Uint8 alpha = getBarAlpha(g);
            SDL_SetRenderDrawColor(r->renderer, 220, 60, 60, alpha);
            SDL_RenderDrawLine(r->renderer, x, centerY - halfH, x, centerY + halfH);
        }
        
        // Playhead
        int playheadX = barX + (int)(PLAYHEAD_RATIO * BAR_W);
        SDL_SetRenderDrawColor(r->renderer, 255, 100, 100, (Uint8)PLAYHEAD_ALPHA);
        SDL_RenderDrawLine(r->renderer, playheadX, barY - 3, playheadX, barY + BAR_H + 3);
    }
    
    // --- Post-processing: apply brightness based on gradient ---
    float brightness = 0.60f + 0.40f * gradient;

    if (useGL) {
        // GLSL path: copy framebuffer to texture, then draw with shader
        SDL_RenderFlush(r->renderer);

        ensureScreenTexture(r, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, r->screenTexture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);

        glViewport(0, 0, w, h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        glUseProgram(r->shader.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, r->screenTexture);
        glUniform1i(glGetUniformLocation(r->shader.program, "uTexture"), 0);
        glUniform1f(r->shader.brightnessLoc, brightness);

        glBindBuffer(GL_ARRAY_BUFFER, r->shader.quadVBO);
        glVertexAttribPointer(r->shader.posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(r->shader.posLoc);
        glVertexAttribPointer(r->shader.texCoordLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(r->shader.texCoordLoc);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisableVertexAttribArray(r->shader.posLoc);
        glDisableVertexAttribArray(r->shader.texCoordLoc);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glUseProgram(0);

        SDL_GL_SwapWindow(r->window);
    } else {
        // Fallback: SDL texture color mod
        SDL_SetRenderTarget(r->renderer, nullptr);
        SDL_SetRenderDrawColor(r->renderer, 0, 0, 0, 255);
        SDL_RenderClear(r->renderer);
        Uint8 brightByte = (Uint8)(brightness * 255.0f);
        SDL_SetTextureColorMod(r->postProcess, brightByte, brightByte, brightByte);
        SDL_RenderCopy(r->renderer, r->postProcess, nullptr, nullptr);
        SDL_RenderPresent(r->renderer);
    }
}

bool pollEvents(GameState& game, const Timeline& timeline, bool& attack, bool& start, InputState& input) {
    attack = false;
    start = false;
    
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
        
        bool trigger = (e.type == SDL_KEYDOWN) ||
                       (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) ||
                       (e.type == SDL_FINGERDOWN);
        
        if (trigger) {
            attack = true;
            start = true;
        }
    }
    
    // Read current keyboard state for WASD
    const Uint8* keystate = SDL_GetKeyboardState(nullptr);
    input.w = keystate[SDL_SCANCODE_W];
    input.a = keystate[SDL_SCANCODE_A];
    input.s = keystate[SDL_SCANCODE_S];
    input.d = keystate[SDL_SCANCODE_D];
    
    return true;
}

// ============================================================================
// SCORE SYSTEM ADDITIONS - All new code below to minimize merge conflicts
// ============================================================================

// --- Score bar constants ---
static constexpr int SCORE_BAR_H = 8;
static constexpr int SCORE_BAR_W = BAR_W / 2;

// --- Score animation ---
static void updateScoreAnimations(ScoreSystem& sc, float dt) {
    float scoreSpeed = 8.0f;
    float fillSpeed = 6.0f;
    float levelSpeed = 4.0f;

    sc.displayScore += (sc.score - sc.displayScore) * dt * scoreSpeed;
    sc.displayScaleFill += (sc.scaleFill - sc.displayScaleFill) * dt * fillSpeed;

    if (sc.level != sc.displayLevel) {
        sc.levelAnimTimer += dt * levelSpeed;
        if (sc.levelAnimTimer >= 1.0f) {
            sc.displayLevel = sc.level;
            sc.levelAnimTimer = 0.0f;
        }
    }
}

// --- Score bar rendering ---
static void drawScoreBar(SDL_Renderer* r, int screenW, int screenH, const GameState& game) {
    const ScoreSystem& score = game.score;
    
    // Dynamic UI offset based on character movement (more dramatic parallax)
    float uiOffsetX = (game.player.pos.x - game.camera.pos.x) * game.camera.zoom * 0.35f;
    float uiOffsetY = (game.player.pos.y - game.camera.pos.y) * game.camera.zoom * 0.22f;
    
    int barX = (int)((screenW - SCORE_BAR_W) / 2 + uiOffsetX);
    int barY = (int)((screenH - BAR_H) / BAR_Y_RATIO + BAR_H + 12 + uiOffsetY);
    int centerY = barY + SCORE_BAR_H / 2;
    
    auto distort = [](float x) -> float {
        float absX = fabsf(x);
        float sign = x >= 0 ? 1.0f : -1.0f;
        return sign * absX * (1.0f + DISTORTION_FACTOR * absX * absX) / DISTORTION_DIVISOR;
    };
    
    float fillExtent = score.displayScaleFill;
    
    for (int px = 0; px < SCORE_BAR_W; px++) {
        float rat = px / (float)SCORE_BAR_W;
        float d = rat - 0.5f;
        float normalized = d / 0.5f;
        float distorted = distort(normalized);
        float distFromCenter = fabsf(distorted);
        
        if (distFromCenter > fillExtent) continue;
        
        float intensity = 1.0f - distFromCenter * 0.5f;
        int halfH = (int)(intensity * (SCORE_BAR_H / 2));
        if (halfH < 1) halfH = 1;
        
        int x = barX + px;
        
        Uint8 r_val, g_val, b_val;
        int displayLvl = score.displayLevel;
        if (displayLvl == 0) { r_val = 180; g_val = 40; b_val = 40; }
        else if (displayLvl == 1) { r_val = 220; g_val = 60; b_val = 60; }
        else if (displayLvl == 2) { r_val = 255; g_val = 80; b_val = 80; }
        else { r_val = 255; g_val = 120; b_val = 120; }
        
        Uint8 alpha = getBarAlpha(intensity);
        SDL_SetRenderDrawColor(r, r_val, g_val, b_val, alpha);
        SDL_RenderDrawLine(r, x, centerY - halfH, x, centerY + halfH);
    }
    
    // Hit feedback
    if (score.hitFeedbackTimer > 0) {
        float alpha = score.hitFeedbackTimer / 0.5f * 180;
        if (score.lastHitGood) {
            SDL_SetRenderDrawColor(r, 255, 150, 150, (Uint8)alpha);
        } else {
            SDL_SetRenderDrawColor(r, 255, 50, 50, (Uint8)alpha);
        }
        SDL_Rect flashRect = {barX - 4, barY - 4, SCORE_BAR_W + 8, SCORE_BAR_H + 8};
        SDL_RenderDrawRect(r, &flashRect);
    }
}
