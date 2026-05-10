#include "render.hpp"
#include <SDL.h>
#include <cmath>
#include <algorithm>

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
static constexpr float SWORD_RECOVER_SPEED = 10.0f;
static constexpr float CHAIN_WINDOW = 0.20f;
static constexpr float ANGLE_ROTATION_SPEED = 12.0f;
static constexpr float CAMERA_FOLLOW_SPEED = 5.0f;
static constexpr float CAMERA_ZOOM_SPEED = 4.0f;
static constexpr float RIBBON_LIFETIME_IDLE = 0.3f;
static constexpr float RIBBON_LIFETIME_CHARGE = 0.35f;
static constexpr float RIBBON_LIFETIME_SLASH = 0.7f;
static constexpr float RIBBON_LIFETIME_RECOVER = 0.4f;
static constexpr int RIBBON_SEGMENTS_IDLE = 3;
static constexpr int RIBBON_SEGMENTS_CHARGE = 5;
static constexpr int RIBBON_SEGMENTS_SLASH = 20;
static constexpr int RIBBON_SEGMENTS_RECOVER = 6;
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
static constexpr Uint32 WINDOW_FLAGS_NATIVE = SDL_WINDOW_RESIZABLE;
static constexpr Uint32 WINDOW_FLAGS_EMSCRIPTEN = SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_RESIZABLE;

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
    bool justLanded = !game.player.jumping && g_swordAnim.wasJumping;
    g_swordAnim.wasJumping = game.player.jumping;

    if (justLanded) {
        g_swordAnim.landTime = game.gameTime;
        g_swordAnim.lastAttackEndOffset = game.player.swordOffset;
    }

    float timeSinceLand = game.gameTime - g_swordAnim.landTime;
    bool canChain = timeSinceLand < CHAIN_WINDOW;

    // --- Unified attack progress (0.0 = start, 1.0 = end) ---
    float attackProgress = 0.0f;
    if (game.player.jumping) {
        attackProgress = 1.0f - (game.player.jumpTimer / game.player.jumpDuration);
        attackProgress = fmaxf(0.0f, fminf(1.0f, attackProgress));
    }

    // --- Direction init (once per jump) ---
    if (justStartedJump) {
        float currentOffset = canChain ? g_swordAnim.lastAttackEndOffset : game.player.swordOffset;
        float normalized = angleDiff(currentOffset, 0.0f);
        g_swordAnim.slashFromLeft = normalized > 0.0f;
        g_swordAnim.chargeStartOffset = game.player.swordOffset;

        if (canChain && fabsf(g_swordAnim.lastAttackEndOffset) > 0.3f) {
            g_swordAnim.chargeStartOffset = g_swordAnim.lastAttackEndOffset;
        }
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

    // --- Sword offset (driven by unified attackProgress) ---
    float windupOffset = g_swordAnim.slashFromLeft ? SWORD_WINDUP_LEFT : SWORD_WINDUP_RIGHT;
    float endOffset = g_swordAnim.slashFromLeft ? SWORD_END_RIGHT : SWORD_END_LEFT;

    float targetSwordOffset;
    if (g_swordAnim.phase == SwordPhase::IDLE) {
        float sway = sinf(game.gameTime * SWORD_IDLE_SWAY_FREQ) * SWORD_IDLE_SWAY_AMP;
        targetSwordOffset = SWORD_IDLE_BASE_ANGLE + sway;
    } else if (g_swordAnim.phase == SwordPhase::CHARGE) {
        // Charge: sword held at windup position on character's back
        // No arc — just ensure it's at the back; smooth speed handles transition
        targetSwordOffset = windupOffset;
    } else if (g_swordAnim.phase == SwordPhase::SLASH) {
        float t = easeOutCubic(phaseProgress);
        targetSwordOffset = windupOffset + (endOffset - windupOffset) * t;
    } else { // RECOVER
        float t = easeOutQuad(phaseProgress);
        // Shortest path back to idle (through the back, never through front)
        float diffToIdle = angleDiff(SWORD_IDLE_BASE_ANGLE, endOffset);
        targetSwordOffset = endOffset + diffToIdle * t;
    }

    // Smooth interpolation to target
    float diff = angleDiff(targetSwordOffset, game.player.swordOffset);
    float speed;
    if (g_swordAnim.phase == SwordPhase::SLASH) {
        speed = SWORD_SLASH_SPEED;
    } else if (g_swordAnim.phase == SwordPhase::CHARGE) {
        speed = SWORD_CHARGE_SPEED;
    } else if (g_swordAnim.phase == SwordPhase::RECOVER) {
        speed = SWORD_RECOVER_SPEED;
    } else {
        speed = SWORD_IDLE_SMOOTH_SPEED;
    }
    game.player.swordOffset += diff * dt * speed;

    // --- Hit detection (synced to unified timeline) ---
    if (g_swordAnim.phase == SwordPhase::SLASH && !game.player.hasSlashed) {
        if (phaseProgress >= 0.45f) {
            game.player.hasSlashed = true;
        }
    }

    // Smooth player angle rotation
    Vec2 moveDir = game.player.pos - game.player.jumpStart;
    if (game.player.jumping && moveDir.len() > 0.01f) {
        float targetAngle = atan2f(moveDir.y, moveDir.x);
        float ad = angleDiff(targetAngle, game.player.angle);
        game.player.angle += ad * dt * ANGLE_ROTATION_SPEED;
    }

    // Camera smoothing
    Vec2 diffCam = game.player.pos - game.camera.pos;
    game.camera.pos = game.camera.pos + diffCam * dt * CAMERA_FOLLOW_SPEED;
    float targetZoom = game.player.jumping ? 1.15f : 1.3f;
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
            segments = RIBBON_SEGMENTS_RECOVER;
            ribbonLifetime = RIBBON_LIFETIME_RECOVER;
            ribbonIntensity = 0.6f;
            break;
        default:
            segments = RIBBON_SEGMENTS_IDLE;
            ribbonLifetime = RIBBON_LIFETIME_IDLE;
            ribbonIntensity = 0.25f;
            break;
    }

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
}

// --- Renderer implementation ---
struct Renderer {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    int width = 800;
    int height = 600;
};

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
    
    return r;
}

void destroyRenderer(Renderer* r) {
    if (!r) return;
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
            
            Uint8 ghostAlpha = (Uint8)(curr.gradient * 255.0f * lifeRatio);
            
            Vec2 prevBase = getWorldToScreen(game, prev.base, w, h);
            Vec2 prevTip = getWorldToScreen(game, prev.tip, w, h);
            Vec2 currBase = getWorldToScreen(game, curr.base, w, h);
            Vec2 currTip = getWorldToScreen(game, curr.tip, w, h);
            
            // Interpolate between stored ribbon positions for smooth curves
            // More steps for high-intensity phases (slash)
            int interpSteps = (curr.gradient > 0.8f) ? 6 : (curr.gradient > 0.5f ? 3 : 1);
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
            if (!e.alive) continue;
            Vec2 s = getWorldToScreen(game, e.pos, w, h);
            float rad = e.radius * game.camera.zoom;
            Uint8 a = e.flashTimer > 0 ? 255 : (e.beingBlown ? 255 : baseAlpha);
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
    }
    
    // Progress bar (always rendered)
    SDL_SetRenderDrawBlendMode(r->renderer, SDL_BLENDMODE_BLEND);
    
    int barX = (w - BAR_W) / 2;
    int barY = (int)((h - BAR_H) / BAR_Y_RATIO);
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
            
            setColor(r->renderer, getBarAlpha(g));
            SDL_RenderDrawLine(r->renderer, x, centerY - halfH, x, centerY + halfH);
        }
        
        // Center line
        setColor(r->renderer, (Uint8)CENTER_LINE_ALPHA);
        SDL_RenderDrawLine(r->renderer, barX, centerY, barX + BAR_W, centerY);
        
        // Playhead
        int playheadX = barX + (int)(PLAYHEAD_RATIO * BAR_W);
        setColor(r->renderer, (Uint8)PLAYHEAD_ALPHA);
        SDL_RenderDrawLine(r->renderer, playheadX, barY - 3, playheadX, barY + BAR_H + 3);
    }
    
    SDL_RenderPresent(r->renderer);
}

bool pollEvents(GameState& game, const Timeline& timeline, bool& attack, bool& start) {
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
    return true;
}
