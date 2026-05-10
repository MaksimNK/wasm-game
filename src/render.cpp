#include "render.hpp"
#include <SDL.h>
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
static constexpr float SWORD_WINDUP_ANGLE = -M_PI * 0.89f;
static constexpr float SWORD_SLASH_END_ANGLE = M_PI * 0.67f;
static constexpr float SWORD_WINDUP_SPEED = 8.0f;
static constexpr float SWORD_SLASH_SPEED = 25.0f;
static constexpr float SWORD_IDLE_FREQ = 2.5f;
static constexpr float SWORD_IDLE_AMP = 0.3f;
static constexpr float SWORD_IDLE_SPEED = 6.0f;
static constexpr float SLASH_PHASE_DURATION = 0.4f;
static constexpr float ANGLE_ROTATION_SPEED = 12.0f;
static constexpr float CAMERA_FOLLOW_SPEED = 5.0f;
static constexpr float CAMERA_ZOOM_SPEED = 4.0f;
static constexpr float RIBBON_LIFETIME = 0.5f;
static constexpr int RIBBON_SEGMENTS_IDLE = 2;
static constexpr int RIBBON_SEGMENTS_JUMP = 8;
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

// --- Forward declarations (score system additions at end of file) ---
static void updateScoreAnimations(ScoreSystem& sc, float dt);
static void drawScoreBar(SDL_Renderer* r, int screenW, int screenH, const ScoreSystem& score);

// --- Animation helpers ---

static float angleDiff(float a, float b) {
    float diff = a - b;
    while (diff > M_PI) diff -= 2.0f * M_PI;
    while (diff < -M_PI) diff += 2.0f * M_PI;
    return diff;
}

// --- Animation system (render team owns this) ---

void updateAnimations(GameState& game, float dt) {
    // Sword animation: smooth transitions between windup / slash / idle
    float targetSwordOffset;
    if (game.player.jumping) {
        float distToTarget = (game.player.jumpTarget - game.player.pos).len();
        float startDist = (game.player.jumpTarget - game.player.jumpStart).len();
        float progress = 1.0f - distToTarget / (startDist + 0.001f);
        progress = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);

        if (!game.player.hasSlashed) {
            float t = progress * progress * (3.0f - 2.0f * progress);
            targetSwordOffset = SWORD_WINDUP_ANGLE * t;
            if (distToTarget < 70.0f) game.player.hasSlashed = true;
        } else {
            float slashProgress = (game.player.jumpDuration - game.player.jumpTimer)
                                  / (game.player.jumpDuration * SLASH_PHASE_DURATION);
            slashProgress = slashProgress < 0.0f ? 0.0f : (slashProgress > 1.0f ? 1.0f : slashProgress);
            float t = slashProgress * slashProgress;
            targetSwordOffset = SWORD_WINDUP_ANGLE + (SWORD_SLASH_END_ANGLE - SWORD_WINDUP_ANGLE) * t;
            
            // Apply WASD sword angle offset during slash phase
            if (game.player.swordCategory != SwordCategory::Default) {
                // swordAngleOffset is stored as absolute aim angle
                // Convert to offset relative to player facing angle (jump direction)
                float baseAngle = game.player.angle;
                float aimAngle = game.player.swordAngleOffset;
                float relativeOffset = angleDiff(aimAngle, baseAngle);
                targetSwordOffset += relativeOffset;
            }
        }
    } else {
        targetSwordOffset = sinf(game.gameTime * SWORD_IDLE_FREQ) * SWORD_IDLE_AMP;
    }

    float diff = angleDiff(targetSwordOffset, game.player.swordOffset);
    float speed = game.player.jumping
        ? (game.player.hasSlashed ? SWORD_SLASH_SPEED : SWORD_WINDUP_SPEED)
        : SWORD_IDLE_SPEED;
    game.player.swordOffset += diff * dt * speed;

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

    // Ribbon trail generation
    float swordAngle = game.player.angle + game.player.swordOffset;
    Vec2 swordBase = game.player.pos;
    Vec2 swordTip(
        game.player.pos.x + cosf(swordAngle) * SWORD_LENGTH,
        game.player.pos.y + sinf(swordAngle) * SWORD_LENGTH
    );

    int segments = game.player.jumping ? RIBBON_SEGMENTS_JUMP : RIBBON_SEGMENTS_IDLE;
    for (int i = 0; i < segments; i++) {
        SwordRibbon ribbon;
        ribbon.base = swordBase;
        ribbon.tip = swordTip;
        ribbon.lifetime = RIBBON_LIFETIME;
        ribbon.maxLifetime = RIBBON_LIFETIME;
        ribbon.gradient = 0.5f; // animation team sets visual intensity
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
        
        // Sword ribbon trail
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
            
            setColor(r->renderer, ghostAlpha);
            renderRibbon(r->renderer, prevBase, prevTip, currBase, currTip, ghostAlpha);
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
        
        // Sword
        Uint8 swordAlpha = game.player.jumping ? 255 : (Uint8)(baseAlpha * 0.9f);
        drawSword(r->renderer, ps.x, ps.y, game.player.angle + game.player.swordOffset, swordAlpha);
        
        // Score bar
        drawScoreBar(r->renderer, w, h, game.score);
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
static constexpr int SCORE_BAR_W = 200;
static constexpr int SCORE_BAR_H = 20;
static constexpr int SCORE_BAR_MARGIN = 20;

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

// --- 7-segment digit drawing ---
static void drawDigit(SDL_Renderer* r, int digit, int x, int y, int w, int h, Uint8 alpha) {
    static const int segments[10] = {
        0b1110111, // 0
        0b0010010, // 1
        0b1011101, // 2
        0b1011011, // 3
        0b0111010, // 4
        0b1101011, // 5
        0b1101111, // 6
        0b1010010, // 7
        0b1111111, // 8
        0b1111011, // 9
    };
    int seg = segments[digit % 10];
    int sw = w / 5;
    int sh = h / 8;
    int mw = w / 2;
    int mh = h / 2;
    SDL_SetRenderDrawColor(r, 255, 255, 255, alpha);
    if (seg & 0b1000000) SDL_RenderDrawLine(r, x + sw, y, x + w - sw, y);
    if (seg & 0b0100000) SDL_RenderDrawLine(r, x, y + sh, x, y + mh - sh);
    if (seg & 0b0010000) SDL_RenderDrawLine(r, x + w, y + sh, x + w, y + mh - sh);
    if (seg & 0b0001000) SDL_RenderDrawLine(r, x + sw, y + mh, x + w - sw, y + mh);
    if (seg & 0b0000100) SDL_RenderDrawLine(r, x, y + mh + sh, x, y + h - sh);
    if (seg & 0b0000010) SDL_RenderDrawLine(r, x + w, y + mh + sh, x + w, y + h - sh);
    if (seg & 0b0000001) SDL_RenderDrawLine(r, x + sw, y + h, x + w - sw, y + h);
}

static void drawNumber(SDL_Renderer* r, int number, int x, int y, int digitW, int digitH, Uint8 alpha) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", number);
    int len = 0;
    while (buf[len]) len++;
    int totalW = len * digitW + (len - 1) * 4;
    int startX = x - totalW;
    for (int i = 0; i < len; i++) {
        drawDigit(r, buf[i] - '0', startX + i * (digitW + 4), y, digitW, digitH, alpha);
    }
}

// --- Score bar rendering ---
static void drawScoreBar(SDL_Renderer* r, int screenW, int screenH, const ScoreSystem& score) {
    int barX = SCORE_BAR_MARGIN;
    int barY = (int)((screenH - BAR_H) / BAR_Y_RATIO) - SCORE_BAR_H - 8;

    SDL_Rect bgRect = {barX, barY, SCORE_BAR_W, SCORE_BAR_H};
    SDL_SetRenderDrawColor(r, 20, 20, 20, 230);
    SDL_RenderFillRect(r, &bgRect);

    int fillW = (int)(SCORE_BAR_W * score.displayScaleFill);
    if (fillW > 0) {
        Uint8 r_val, g_val, b_val;
        int displayLvl = score.displayLevel;
        if (displayLvl == 0) { r_val = 255; g_val = 255; b_val = 255; }
        else if (displayLvl == 1) { r_val = 0; g_val = 255; b_val = 255; }
        else if (displayLvl == 2) { r_val = 255; g_val = 0; b_val = 255; }
        else { r_val = 255; g_val = 215; b_val = 0; }

        SDL_SetRenderDrawColor(r, r_val, g_val, b_val, 255);
        SDL_Rect fillRect = {barX, barY, fillW, SCORE_BAR_H};
        SDL_RenderFillRect(r, &fillRect);
    }

    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDrawRect(r, &bgRect);

    SDL_SetRenderDrawColor(r, 180, 180, 180, 255);
    SDL_Rect borderRect = {barX - 2, barY - 2, SCORE_BAR_W + 4, SCORE_BAR_H + 4};
    SDL_RenderDrawRect(r, &borderRect);

    if (score.hitFeedbackTimer > 0) {
        float alpha = score.hitFeedbackTimer / 0.5f * 180;
        if (score.lastHitGood) {
            SDL_SetRenderDrawColor(r, 255, 255, 255, (Uint8)alpha);
        } else {
            SDL_SetRenderDrawColor(r, 255, 50, 50, (Uint8)alpha);
        }
        SDL_Rect flashRect = {barX - 4, barY - 4, SCORE_BAR_W + 8, SCORE_BAR_H + 8};
        SDL_RenderDrawRect(r, &flashRect);
    }

    int scoreNum = (int)score.displayScore;
    int digitW = 8;
    int digitH = 14;
    int scoreTextY = barY + SCORE_BAR_H + 6;
    int scoreTextX = barX + SCORE_BAR_W;
    drawNumber(r, scoreNum, scoreTextX, scoreTextY, digitW, digitH, 255);
}
