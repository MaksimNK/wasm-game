#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include "audio.hpp"
#include "game.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// --- App constants ---
static constexpr const char* APP_NAME = "Rhythm Slayer";
static constexpr const char* DEFAULT_AUDIO_PATH = "audio/test-4.mp3";
static constexpr int DEFAULT_WIDTH = 800;
static constexpr int DEFAULT_HEIGHT = 600;

// --- Render constants ---
static constexpr Uint8 BASE_ALPHA_MIN = 80;
static constexpr Uint8 BASE_ALPHA_RANGE = 175;
static constexpr float RIBBON_LIFETIME_BLEND = 0.5f;
static constexpr int RIBBON_STEPS = 5;

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

// --- Audio constants ---
static constexpr int AUDIO_SAMPLES = 4096;
static constexpr SDL_AudioFormat AUDIO_FORMAT = AUDIO_F32SYS;
static constexpr int AUDIO_CHANNELS = 1;

// --- State ---
static SDL_Window* g_window = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static Timeline g_timeline;
static GameState g_game;
static AudioPlayer g_audioPlayer;
static float g_time = 0.0f;
static bool g_running = false;
static bool g_hasAudio = false;
static Uint32 g_start = 0;

static std::vector<uint8_t> loadFile(const char* path) {
    std::vector<uint8_t> data;
    FILE* f = fopen(path, "rb");
    if (!f) return data;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    data.resize(sz);
    fread(data.data(), 1, sz, f);
    fclose(f);
    return data;
}

static void startAudio() {
    if (g_running) return;
    g_running = true;
    g_audioPlayer.start();
}

// --- Drawing primitives ---

static void setColor(Uint8 alpha) {
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, alpha);
}

static void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
    SDL_RenderDrawLine(g_renderer, (int)x1, (int)y1, (int)x2, (int)y2);
    SDL_RenderDrawLine(g_renderer, (int)x2, (int)y2, (int)x3, (int)y3);
    SDL_RenderDrawLine(g_renderer, (int)x3, (int)y3, (int)x1, (int)y1);
}

static void fillTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
    // Sort vertices by Y
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
        SDL_RenderDrawLine(g_renderer, (int)xLeft, y, (int)xRight, y);
    }
}

static void drawPlayer(float cx, float cy, float angle, Uint8 alpha) {
    float r = PLAYER_TRI_SIZE;
    float frontX = cx + cosf(angle) * r * PLAYER_TRI_FRONT;
    float frontY = cy + sinf(angle) * r * PLAYER_TRI_FRONT;
    float leftX = cx + cosf(angle + PLAYER_TRI_REAR_ANGLE) * r;
    float leftY = cy + sinf(angle + PLAYER_TRI_REAR_ANGLE) * r;
    float rightX = cx + cosf(angle - PLAYER_TRI_REAR_ANGLE) * r;
    float rightY = cy + sinf(angle - PLAYER_TRI_REAR_ANGLE) * r;
    
    setColor(alpha);
    fillTriangle(frontX, frontY, leftX, leftY, rightX, rightY);
    // Outline with same alpha
    drawTriangle(frontX, frontY, leftX, leftY, rightX, rightY);
}

static void drawSword(float cx, float cy, float angle, Uint8 alpha) {
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
    
    setColor(alpha);
    fillTriangle(x1, y1, x2, y2, tip.x, tip.y);
    drawTriangle(x1, y1, x2, y2, tip.x, tip.y);
}

static void drawCircle(float cx, float cy, float radius) {
    float step = 2.0f * M_PI / CIRCLE_SEGMENTS;
    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
        float a1 = i * step;
        float a2 = (i + 1) * step;
        SDL_RenderDrawLine(g_renderer,
            (int)(cx + cosf(a1) * radius), (int)(cy + sinf(a1) * radius),
            (int)(cx + cosf(a2) * radius), (int)(cy + sinf(a2) * radius));
    }
}

static void fillCircle(float cx, float cy, float radius) {
    int r = (int)(radius + 0.5f);
    for (int y = -r; y <= r; y++) {
        float dy = (float)y;
        float dx = sqrtf(std::max(0.0f, radius * radius - dy * dy));
        if (dx > 0.5f) {
            SDL_RenderDrawLine(g_renderer, (int)(cx - dx), (int)(cy + y), (int)(cx + dx), (int)(cy + y));
        }
    }
}

static void drawEnemy(float cx, float cy, float radius, Uint8 alpha, bool beingBlown) {
    setColor(alpha);
    fillCircle(cx, cy, radius);
    // Inner detail at half alpha
    setColor((Uint8)(alpha * 0.5f));
    drawCircle(cx, cy, radius * CIRCLE_INNER_RATIO);
}

// --- Game rendering ---

static void renderRibbon(const Vec2& prevBase, const Vec2& prevTip,
                         const Vec2& currBase, const Vec2& currTip, Uint8 alpha) {
    // Draw filled ribbon as a quad using scanlines
    // Find bounding box
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
            SDL_RenderDrawLine(g_renderer, (int)xMin, py, (int)xMax, py);
        }
    }
}

static void renderGame(int w, int h, float gradient) {
    Uint8 baseAlpha = (Uint8)(BASE_ALPHA_MIN + gradient * BASE_ALPHA_RANGE);
    
    // Sword ribbon trail
    for (size_t i = 1; i < g_game.player.swordRibbons.size(); i++) {
        const auto& prev = g_game.player.swordRibbons[i-1];
        const auto& curr = g_game.player.swordRibbons[i];
        
        float lifeRatio = (prev.lifetime + curr.lifetime) * RIBBON_LIFETIME_BLEND / prev.maxLifetime;
        if (lifeRatio > 1.0f) lifeRatio = 1.0f;
        if (lifeRatio <= 0) continue;
        
        Uint8 ghostAlpha = (Uint8)(curr.gradient * 255.0f * lifeRatio);
        
        Vec2 prevBase = getWorldToScreen(g_game, prev.base, w, h);
        Vec2 prevTip = getWorldToScreen(g_game, prev.tip, w, h);
        Vec2 currBase = getWorldToScreen(g_game, curr.base, w, h);
        Vec2 currTip = getWorldToScreen(g_game, curr.tip, w, h);
        
        setColor(ghostAlpha);
        renderRibbon(prevBase, prevTip, currBase, currTip, ghostAlpha);
    }
    
    // Enemies
    for (const auto& e : g_game.enemies) {
        if (!e.alive) continue;
        Vec2 s = getWorldToScreen(g_game, e.pos, w, h);
        float r = e.radius * g_game.camera.zoom;
        Uint8 a = e.flashTimer > 0 ? 255 : (e.beingBlown ? 255 : baseAlpha);
        drawEnemy(s.x, s.y, r, a, e.beingBlown);
    }
    
    // Player
    Vec2 ps = getWorldToScreen(g_game, g_game.player.pos, w, h);
    drawPlayer(ps.x, ps.y, g_game.player.angle, baseAlpha);
    
    // Sword
    Uint8 swordAlpha = g_game.player.jumping ? 255 : (Uint8)(baseAlpha * 0.9f);
    drawSword(ps.x, ps.y, g_game.player.angle + g_game.player.swordOffset, swordAlpha);
}

// --- Progress bar ---

static Uint8 getBarAlpha(float gradient) {
    if (gradient < ALPHA_LOW_THRESHOLD) {
        return (Uint8)(gradient / ALPHA_LOW_DIVISOR * ALPHA_LOW_MAX);
    }
    return (Uint8)(ALPHA_HIGH_OFFSET + (gradient - ALPHA_LOW_THRESHOLD) / ALPHA_HIGH_DIVISOR * ALPHA_HIGH_MAX);
}

static void renderProgressBar(int w, int h) {
    int barX = (w - BAR_W) / 2;
    int barY = (int)((h - BAR_H) / BAR_Y_RATIO);
    int centerY = barY + BAR_H / 2;
    
    float currentTime = g_running ? g_time : 0.0f;
    
    if (!g_timeline.gradient.empty()) {
        auto distort = [](float x) -> float {
            float absX = fabsf(x);
            float sign = x >= 0 ? 1.0f : -1.0f;
            return sign * absX * (1.0f + DISTORTION_FACTOR * absX * absX) / DISTORTION_DIVISOR;
        };
        
        int numFrames = g_timeline.gradient.size();
        for (int px = 0; px < BAR_W; px++) {
            float r = px / (float)BAR_W;
            float d = r - PLAYHEAD_RATIO;
            float maxD = d >= 0 ? (1.0f - PLAYHEAD_RATIO) : PLAYHEAD_RATIO;
            float normalized = d / maxD;
            float distorted = distort(normalized);
            float t = currentTime + distorted * maxD * WINDOW_SECONDS;
            
            int frameIdx = (int)(t * g_timeline.fps);
            if (frameIdx < 0 || frameIdx >= numFrames) continue;
            
            float g = g_timeline.gradient[frameIdx];
            int halfH = (int)(g * (BAR_H / 2));
            if (halfH < 1) halfH = 1;
            int x = barX + px;
            
            setColor(getBarAlpha(g));
            SDL_RenderDrawLine(g_renderer, x, centerY - halfH, x, centerY + halfH);
        }
        
        // Center line synced to beat (visible when gradient is low)
        setColor((Uint8)CENTER_LINE_ALPHA);
        SDL_RenderDrawLine(g_renderer, barX, centerY, barX + BAR_W, centerY);
        
        // Playhead
        int playheadX = barX + (int)(PLAYHEAD_RATIO * BAR_W);
        setColor((Uint8)PLAYHEAD_ALPHA);
        SDL_RenderDrawLine(g_renderer, playheadX, barY - 3, playheadX, barY + BAR_H + 3);
    }
}

// --- Main loop ---

static void getScreenSize(int& w, int& h) {
    SDL_GetRendererOutputSize(g_renderer, &w, &h);
    if (w <= 0 || h <= 0) SDL_GetWindowSize(g_window, &w, &h);
    if (w <= 0 || h <= 0) { w = DEFAULT_WIDTH; h = DEFAULT_HEIGHT; }
}

static void render() {
    int w, h;
    getScreenSize(w, h);
    
    float gradient = getBrightnessAtTime(g_timeline, g_time);
    
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    
    if (g_running) {
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        renderGame(w, h, gradient);
    }
    
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    renderProgressBar(w, h);
    
    SDL_RenderPresent(g_renderer);
}

static bool processEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
        
        bool attackTriggered = (e.type == SDL_KEYDOWN) ||
                               (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) ||
                               (e.type == SDL_FINGERDOWN);
        
        if (!g_running) {
            if (attackTriggered) {
                startAudio();
                initGame(g_game, DEFAULT_WIDTH, DEFAULT_HEIGHT);
            }
        } else {
            if (attackTriggered) {
                processAttack(g_game, g_timeline);
            }
        }
    }
    return true;
}

static void mainLoop() {
    if (!processEvents()) {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
        SDL_CloseAudio();
        SDL_Quit();
        exit(0);
    }
    
    Uint32 now = SDL_GetTicks();
    static Uint32 lastTime = now;
    float realDt = (now - lastTime) / 1000.0f;
    lastTime = now;
    if (realDt > 0.1f) realDt = 0.1f;
    
    if (g_running) {
        if (g_hasAudio) {
            g_time = g_audioPlayer.getPlaybackTime();
        } else {
            g_time = (SDL_GetTicks() - g_start) / 1000.0f;
        }
        
        updateGame(g_game, g_timeline, realDt, g_time);
    }
    
    render();
}

int main(int argc, char* argv[]) {
    const char* path = DEFAULT_AUDIO_PATH;
    if (argc > 1) path = argv[1];
    
    auto mp3 = loadFile(path);
    
    if (mp3.empty()) {
        fprintf(stderr, "[ERROR] Failed to load audio: %s\n", path);
        return 1;
    }
    
    AudioDecoder dec;
    if (!dec.loadMP3(mp3.data(), mp3.size())) {
        fprintf(stderr, "[ERROR] Failed to decode MP3\n");
        return 1;
    }
    
    g_timeline = analyzeAudio(dec.getSamples(), dec.getSampleRate());
    g_audioPlayer.setSamples(dec.getSamples(), dec.getSampleRate());
    g_hasAudio = true;
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "[ERROR] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    
#ifdef __EMSCRIPTEN__
    g_window = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                0, 0, WINDOW_FLAGS_EMSCRIPTEN);
#else
    g_window = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                DEFAULT_WIDTH, DEFAULT_HEIGHT, WINDOW_FLAGS_NATIVE);
#endif
    if (!g_window) { SDL_Quit(); return 1; }
    
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!g_renderer) { SDL_DestroyWindow(g_window); SDL_Quit(); return 1; }
    
    if (g_audioPlayer.samples && g_audioPlayer.totalSamples > 0) {
        SDL_AudioSpec want, have;
        SDL_memset(&want, 0, sizeof(want));
        want.freq = g_audioPlayer.sampleRate;
        want.format = AUDIO_FORMAT;
        want.channels = AUDIO_CHANNELS;
        want.samples = AUDIO_SAMPLES;
        want.callback = sdlAudioCallback;
        want.userdata = &g_audioPlayer;
        if (SDL_OpenAudio(&want, &have) >= 0) {
            printf("[INFO] Audio ready: %d Hz\n", g_audioPlayer.sampleRate);
        } else {
            fprintf(stderr, "[WARN] Failed to open audio: %s\n", SDL_GetError());
        }
    }
    
    g_start = SDL_GetTicks();
    
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(mainLoop, 0, 1);
#else
    while (processEvents()) mainLoop();
#endif
    
    SDL_CloseAudio();
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
