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

// Audio
struct AudioState {
    const float* samples;
    int totalSamples;
    int currentSample;
    bool started;
    int sampleRate;
    Uint32 lastCallbackTime;
    int lastCallbackSample;
};
static AudioState g_audio = {nullptr, 0, 0, false, 44100, 0, 0};
static std::vector<float> g_audioSamples;

void audioCallback(void*, Uint8* stream, int len) {
    if (!g_audio.started || !g_audio.samples || g_audio.currentSample >= g_audio.totalSamples) {
        memset(stream, 0, len);
        return;
    }
    int n = len / sizeof(float);
    int rem = g_audio.totalSamples - g_audio.currentSample;
    int copy = std::min(n, rem);
    memcpy(stream, &g_audio.samples[g_audio.currentSample], copy * sizeof(float));
    if (copy < n) memset(stream + copy * sizeof(float), 0, (n - copy) * sizeof(float));
    g_audio.currentSample += copy;
    g_audio.lastCallbackTime = SDL_GetTicks();
    g_audio.lastCallbackSample = g_audio.currentSample;
}

// Global state
SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
Timeline g_timeline;
GameState g_game;
float g_time = 0.0f;
bool g_running = false;
bool g_hasAudio = false;
Uint32 g_start = 0;

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
    g_audio.started = true;
    g_audio.lastCallbackTime = SDL_GetTicks();
    g_audio.lastCallbackSample = 0;
    if (g_audio.samples && g_audio.totalSamples > 0) {
        SDL_PauseAudio(0);
    }
}

static void drawTriangle(SDL_Renderer* r, float x1, float y1, float x2, float y2, float x3, float y3) {
    SDL_RenderDrawLine(r, (int)x1, (int)y1, (int)x2, (int)y2);
    SDL_RenderDrawLine(r, (int)x2, (int)y2, (int)x3, (int)y3);
    SDL_RenderDrawLine(r, (int)x3, (int)y3, (int)x1, (int)y1);
}

static void fillTriangle(SDL_Renderer* r, float x1, float y1, float x2, float y2, float x3, float y3) {
    int minY = (int)std::min(std::min(y1, y2), y3);
    int maxY = (int)std::max(std::max(y1, y2), y3);
    for (int y = minY; y <= maxY; y++) {
        float xStart = 99999, xEnd = -99999;
        auto edge = [&](float ax, float ay, float bx, float by) {
            if ((ay <= y && by > y) || (by <= y && ay > y)) {
                float t = (y - ay) / (by - ay);
                float x = ax + t * (bx - ax);
                xStart = std::min(xStart, x);
                xEnd = std::max(xEnd, x);
            }
        };
        edge(x1, y1, x2, y2);
        edge(x2, y2, x3, y3);
        edge(x3, y3, x1, y1);
        if (xEnd > xStart) {
            SDL_RenderDrawLine(r, (int)xStart, y, (int)xEnd, y);
        }
    }
}

static void drawCircle(SDL_Renderer* r, float cx, float cy, float radius, int segments) {
    float angleStep = 2.0f * M_PI / segments;
    for (int i = 0; i < segments; i++) {
        float a1 = i * angleStep;
        float a2 = (i + 1) * angleStep;
        SDL_RenderDrawLine(r, 
            (int)(cx + cosf(a1) * radius), (int)(cy + sinf(a1) * radius),
            (int)(cx + cosf(a2) * radius), (int)(cy + sinf(a2) * radius));
    }
}

static void fillCircle(SDL_Renderer* r, float cx, float cy, float radius) {
    for (float y = cy - radius; y <= cy + radius; y += 1.0f) {
        float dy = y - cy;
        float dx = sqrtf(std::max(0.0f, radius * radius - dy * dy));
        if (dx > 0.5f) {
            SDL_RenderDrawLine(r, (int)(cx - dx), (int)y, (int)(cx + dx), (int)y);
        }
    }
}

static void drawPlayerTriangle(SDL_Renderer* r, float cx, float cy, float angle, float scale, Uint8 alpha) {
    float pr = 12.0f * scale;
    float p1x = cx + cosf(angle) * pr * 1.5f;
    float p1y = cy + sinf(angle) * pr * 1.5f;
    float p2x = cx + cosf(angle + 2.5f) * pr;
    float p2y = cy + sinf(angle + 2.5f) * pr;
    float p3x = cx + cosf(angle - 2.5f) * pr;
    float p3y = cy + sinf(angle - 2.5f) * pr;
    SDL_SetRenderDrawColor(r, 255, 255, 255, alpha);
    fillTriangle(r, p1x, p1y, p2x, p2y, p3x, p3y);
    // Outline fades with fill
    Uint8 outlineAlpha = alpha > 50 ? 255 : alpha * 5;
    if (outlineAlpha > 255) outlineAlpha = 255;
    SDL_SetRenderDrawColor(r, 255, 255, 255, outlineAlpha);
    drawTriangle(r, p1x, p1y, p2x, p2y, p3x, p3y);
}

static void drawSword(SDL_Renderer* r, float cx, float cy, float angle, float scale, Uint8 alpha) {
    float swordLen = 115.0f * scale;
    float swordWidth = 5.0f * scale;
    Vec2 base(cx + cosf(angle) * 5.0f * scale, cy + sinf(angle) * 5.0f * scale);
    Vec2 tip(cx + cosf(angle) * swordLen, cy + sinf(angle) * swordLen);
    Vec2 swordDir = Vec2(tip.x - base.x, tip.y - base.y).normalized();
    Vec2 perp(-swordDir.y, swordDir.x);
    float sx1 = base.x + perp.x * swordWidth;
    float sy1 = base.y + perp.y * swordWidth;
    float sx2 = base.x - perp.x * swordWidth;
    float sy2 = base.y - perp.y * swordWidth;
    SDL_SetRenderDrawColor(r, 255, 255, 255, alpha);
    fillTriangle(r, sx1, sy1, sx2, sy2, tip.x, tip.y);
    // Outline fades with fill
    Uint8 outlineAlpha = alpha > 50 ? 255 : alpha * 5;
    if (outlineAlpha > 255) outlineAlpha = 255;
    SDL_SetRenderDrawColor(r, 255, 255, 255, outlineAlpha);
    drawTriangle(r, sx1, sy1, sx2, sy2, tip.x, tip.y);
}

static void renderGame(int w, int h, float brightness) {
    Uint8 baseAlpha = (Uint8)(80 + brightness * 175);
    
    // Decorative walls
    for (const auto& wall : g_game.walls) {
        Vec2 s = getWorldToScreen(g_game, Vec2(wall.x, wall.y), w, h);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, (Uint8)(15 + brightness * 25));
        SDL_Rect wr = {(int)s.x, (int)s.y, (int)(wall.w * 1.4f), (int)(wall.h * 1.4f)};
        SDL_RenderFillRect(renderer, &wr);
    }
    
    // Sword ribbon trail - curtain/mesh effect connecting consecutive sword positions
    for (size_t i = 1; i < g_game.swordRibbons.size(); i++) {
        const auto& prev = g_game.swordRibbons[i-1];
        const auto& curr = g_game.swordRibbons[i];
        
        // Average lifetime for smooth fade
        float lifeRatio = (prev.lifetime + curr.lifetime) * 0.5f / prev.maxLifetime;
        if (lifeRatio > 1.0f) lifeRatio = 1.0f;
        if (lifeRatio <= 0) continue;
        
        Uint8 baseGhostAlpha = (Uint8)getBarOpacity(curr.brightness);
        Uint8 alpha = (Uint8)(baseGhostAlpha * lifeRatio);
        
        // Get screen positions
        Vec2 prevBase = getWorldToScreen(g_game, prev.base, w, h);
        Vec2 prevTip = getWorldToScreen(g_game, prev.tip, w, h);
        Vec2 currBase = getWorldToScreen(g_game, curr.base, w, h);
        Vec2 currTip = getWorldToScreen(g_game, curr.tip, w, h);
        
        // Draw ribbon as two triangles forming a quad
        // Triangle 1: prevBase, prevTip, currBase
        // Triangle 2: prevTip, currTip, currBase
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
        
        // Fill the quad with scanlines for solid look
        // Interpolate between the two sword lines
        int steps = 5;
        for (int s = 0; s < steps; s++) {
            float t1 = s / (float)steps;
            float t2 = (s + 1) / (float)steps;
            
            Vec2 b1 = Vec2(
                prevBase.x + (currBase.x - prevBase.x) * t1,
                prevBase.y + (currBase.y - prevBase.y) * t1
            );
            Vec2 t1p = Vec2(
                prevTip.x + (currTip.x - prevTip.x) * t1,
                prevTip.y + (currTip.y - prevTip.y) * t1
            );
            Vec2 b2 = Vec2(
                prevBase.x + (currBase.x - prevBase.x) * t2,
                prevBase.y + (currBase.y - prevBase.y) * t2
            );
            Vec2 t2p = Vec2(
                prevTip.x + (currTip.x - prevTip.x) * t2,
                prevTip.y + (currTip.y - prevTip.y) * t2
            );
            
            // Draw slice of the ribbon
            SDL_RenderDrawLine(renderer, (int)b1.x, (int)b1.y, (int)t1p.x, (int)t1p.y);
        }
    }
    
    // Enemies (circles)
    for (const auto& e : g_game.enemies) {
        if (!e.alive) continue;
        Vec2 s = getWorldToScreen(g_game, e.pos, w, h);
        float r = e.radius * g_game.cameraZoom;
        // Being blown away = bright flash
        Uint8 a = e.hitTimer > 0 ? 255 : (e.beingBlown ? 255 : baseAlpha);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, a);
        fillCircle(renderer, s.x, s.y, r);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, (Uint8)(a * 0.5f));
        drawCircle(renderer, s.x, s.y, r * 0.6f, 8);
    }
    
    // Player (triangle)
    Vec2 ps = getWorldToScreen(g_game, g_game.playerPos, w, h);
    drawPlayerTriangle(renderer, ps.x, ps.y, g_game.playerAngle, 1.0f, baseAlpha);
    
    // Sword - attached directly to player
    float swordScale = g_game.swordActive ? 1.3f : 1.0f;
    Uint8 swordAlpha = g_game.swordActive ? 255 : (Uint8)(baseAlpha * 0.9f);
    drawSword(renderer, ps.x, ps.y, g_game.swordAngle, swordScale, swordAlpha);
}

static void renderProgressBar(int w, int h) {
    const int BAR_W = 377;
    const int BAR_H = 17;
    int barX = (w - BAR_W) / 2;
    int barY = (h - BAR_H) / 1.21;
    int centerY = barY + BAR_H / 2;
    const float WINDOW_SECONDS = 0.77f;
    const float PLAYHEAD_RATIO = 0.21f;
    
    float currentTime = g_running ? g_time : 0.0f;
    
    if (!g_timeline.brightness.empty()) {
        auto distort = [&](float x) -> float {
            float absX = fabsf(x);
            float sign = x >= 0 ? 1.0f : -1.0f;
            return sign * absX * (1.0f + 6.0f * absX * absX) / 7.0f;
        };
        
        int numFrames = g_timeline.brightness.size();
        for (int px = 0; px < BAR_W; px++) {
            float r = px / (float)BAR_W;
            float d = r - PLAYHEAD_RATIO;
            float maxD = d >= 0 ? (1.0f - PLAYHEAD_RATIO) : PLAYHEAD_RATIO;
            float normalized = d / maxD;
            float distorted = distort(normalized);
            float t = currentTime + distorted * maxD * WINDOW_SECONDS;
            
            int frameIdx = (int)(t * g_timeline.fps);
            if (frameIdx < 0 || frameIdx >= numFrames) continue;
            
            float b = g_timeline.brightness[frameIdx];
            int halfH = (int)(b * (BAR_H / 2));
            if (halfH < 1) halfH = 1;
            int x = barX + px;
            
            Uint8 alpha = b < 0.3f ? (Uint8)(b / 0.1f * 15) : (Uint8)(15 + (b - 0.3f) / 0.7f * 240);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
            SDL_RenderDrawLine(renderer, x, centerY - halfH, x, centerY + halfH);
        }
        
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 30);
        SDL_RenderDrawLine(renderer, barX, centerY, barX + BAR_W, centerY);
        
        int playheadX = barX + (int)(PLAYHEAD_RATIO * BAR_W);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(renderer, playheadX, barY - 3, playheadX, barY + BAR_H + 3);
    }
    
    // Score bar - proportional, fills from center, capped at reasonable max
    if (g_game.score > 0) {
        int maxScoreDisplay = 1000;
        int displayScore = g_game.score > maxScoreDisplay ? maxScoreDisplay : g_game.score;
        int barWidth = (displayScore * BAR_W) / maxScoreDisplay;
        barWidth = barWidth > BAR_W ? BAR_W : barWidth;
        int startX = barX + (BAR_W - barWidth) / 2;
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
        SDL_Rect scoreBar = {startX, barY - 7, barWidth, 3};
        SDL_RenderFillRect(renderer, &scoreBar);
    }
}

static void render() {
    int w = 800, h = 600;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    if (w <= 0 || h <= 0) SDL_GetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0) { w = 800; h = 600; }
    
    float brightness = getBrightnessAtTime(g_timeline, g_time);
    
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    
    if (g_running) {
        renderGame(w, h, brightness);
    }
    
    renderProgressBar(w, h);
    SDL_RenderPresent(renderer);
}

static bool processEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
        if (!g_running) {
            if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_KEYDOWN || e.type == SDL_FINGERDOWN) {
                startAudio();
                initGame(g_game, 800, 600);
            }
        } else {
            if (e.type == SDL_KEYDOWN) {
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
            float audioTime = (float)g_audio.lastCallbackSample / g_audio.sampleRate;
            float elapsedSinceCallback = (SDL_GetTicks() - g_audio.lastCallbackTime) / 1000.0f;
            g_time = audioTime + elapsedSinceCallback;
        } else {
            g_time = (SDL_GetTicks() - g_start) / 1000.0f;
        }
        
        updateGame(g_game, g_timeline, realDt, g_time);
    }
    
    render();
}

int main(int argc, char* argv[]) {
    printf("Rhythm Slayer\n");
    
    const char* path = "audio/test-4.mp3";
    if (argc > 1) path = argv[1];
    
    auto mp3 = loadFile(path);
    
    if (mp3.empty()) {
        printf("No audio, using test data\n");
        g_timeline.fps = 86.0f;
        for (int i = 0; i < 3000; i++) {
            float b = 0.5f + 0.5f * sinf(i * 0.1f);
            b = powf(b, 2.0f);
            g_timeline.brightness.push_back(b);
        }
    } else {
        printf("Loaded %zu bytes\n", mp3.size());
        
        AudioDecoder dec;
        if (!dec.loadMP3(mp3.data(), mp3.size())) {
            printf("Decode failed\n");
            return 1;
        }
        
        printf("Audio: %d Hz, %zu samples\n", dec.getSampleRate(), dec.getSamples().size());
        g_timeline = analyzeAudio(dec.getSamples(), dec.getSampleRate());
        printf("Timeline: %zu frames\n", g_timeline.brightness.size());
        
        g_audioSamples = dec.getSamples();
        g_audio.samples = g_audioSamples.data();
        g_audio.totalSamples = g_audioSamples.size();
        g_audio.sampleRate = dec.getSampleRate();
        g_hasAudio = true;
    }
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        printf("SDL_Init failed\n");
        return 1;
    }
    
#ifdef __EMSCRIPTEN__
    window = SDL_CreateWindow("Rhythm Slayer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_RESIZABLE);
#else
    window = SDL_CreateWindow("Rhythm Slayer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_RESIZABLE);
#endif
    if (!window) { SDL_Quit(); return 1; }
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    
    if (g_audio.samples && g_audio.totalSamples > 0) {
        SDL_AudioSpec want, have;
        SDL_memset(&want, 0, sizeof(want));
        want.freq = 44100;
        want.format = AUDIO_F32SYS;
        want.channels = 1;
        want.samples = 4096;
        want.callback = audioCallback;
        if (SDL_OpenAudio(&want, &have) >= 0) {
            printf("Audio ready\n");
        }
    }
    
    g_start = SDL_GetTicks();
    
    printf("Click or press key to start\n");
    
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(mainLoop, 0, 1);
#else
    while (processEvents()) mainLoop();
#endif
    
    SDL_CloseAudio();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
