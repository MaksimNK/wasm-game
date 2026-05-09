#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include "audio.hpp"

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
    Uint32 lastCallbackTime;  // SDL_GetTicks() at last callback
    int lastCallbackSample;    // currentSample at last callback
};
static AudioState g_audio = {nullptr, 0, 0, false, 44100, 0, 0};
static std::vector<float> g_audioSamples; // Keeps samples alive

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

// State
SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
Timeline g_timeline;
float g_time = 0.0f;
Uint32 g_start = 0;
bool g_running = false;
bool g_hasAudio = false;

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

static void render() {
    int w = 800, h = 600;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    if (w <= 0 || h <= 0) {
        SDL_GetWindowSize(window, &w, &h);
    }
    if (w <= 0 || h <= 0) {
        w = 800; h = 600;
    }

    // Black background
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // Enable alpha blending for transparency
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Progress bar dimensions
    const int BAR_W = 377;
    const int BAR_H = 17;
    int barX = (w - BAR_W) / 2;
    int barY = (h - BAR_H) / 1.21;
    int centerY = barY + BAR_H / 2;
    const float WINDOW_SECONDS = 0.77f;
    const float PLAYHEAD_RATIO = 0.21f;

    float currentTime = g_running ? g_time : 0.0f;
    float totalDuration = g_timeline.brightness.empty() ? 0.0f : g_timeline.brightness.size() / g_timeline.fps;

    // Draw waveform - symmetric around center line with fisheye distortion
    if (!g_timeline.brightness.empty()) {
        // Fisheye distortion: center is zoomed in (slow), edges compressed (fast)
        // Smooth sigmoid-like curve for symmetric distortion
        auto distort = [&](float x) -> float {
            // x in [-1, 1], aggressive fisheye
            // Using: f(x) = x * (1 + 6*x^2) / 7
            // f'(0) = 1/7 = 0.14 (very zoomed in center)
            // f'(1) = (1 + 18)/7 = 2.71 (very compressed edges)
            float absX = fabsf(x);
            float sign = x >= 0 ? 1.0f : -1.0f;
            return sign * absX * (1.0f + 6.0f * absX * absX) / 7.0f;
        };

        int numFrames = g_timeline.brightness.size();
        for (int px = 0; px < BAR_W; px++) {
            float r = px / (float)BAR_W; // 0 to 1
            float d = r - PLAYHEAD_RATIO; // distance from playhead
            float maxD = d >= 0 ? (1.0f - PLAYHEAD_RATIO) : PLAYHEAD_RATIO;
            float normalized = d / maxD; // [-1, 1] for each side
            float distorted = distort(normalized);
            float t = currentTime + distorted * maxD * WINDOW_SECONDS;

            int frameIdx = (int)(t * g_timeline.fps);
            if (frameIdx < 0 || frameIdx >= numFrames) continue;

            float b = g_timeline.brightness[frameIdx];
            int halfH = (int)(b * (BAR_H / 2));
            if (halfH < 1) halfH = 1;
            int x = barX + px;

            // White with opacity - quiet parts fade out
            Uint8 alpha = b < 0.3f ? (Uint8)(b / 0.1f * 15) : (Uint8)(15 + (b - 0.3f) / 0.7f * 240);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
            // Draw symmetric line up and down from center
            SDL_RenderDrawLine(renderer, x, centerY - halfH, x, centerY + halfH);
        }

        // Horizontal center line
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 30);
        SDL_RenderDrawLine(renderer, barX, centerY, barX + BAR_W, centerY);

        // Playhead - white vertical line
        int playheadX = barX + (int)(PLAYHEAD_RATIO * BAR_W);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(renderer, playheadX, barY - 3, playheadX, barY + BAR_H + 3);
    }

    SDL_RenderPresent(renderer);
}

static bool processEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
        if (!g_running) {
            if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_KEYDOWN || e.type == SDL_FINGERDOWN) {
                startAudio();
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
    if (g_running) {
        if (g_hasAudio) {
            // Audio is master clock: use callback position + interpolation since last callback
            float audioTime = (float)g_audio.lastCallbackSample / g_audio.sampleRate;
            float elapsedSinceCallback = (SDL_GetTicks() - g_audio.lastCallbackTime) / 1000.0f;
            g_time = audioTime + elapsedSinceCallback;
        } else {
            g_time = (SDL_GetTicks() - g_start) / 1000.0f;
        }
    }
    render();
}

int main(int argc, char* argv[]) {
    printf("Audio Visualizer\n");
    
    const char* path = "audio/test.mp3";
    if (argc > 1) path = argv[1];
    
    auto mp3 = loadFile(path);
    
    if (mp3.empty()) {
        printf("No audio, using test data\n");
        g_timeline.fps = 86.0f;
        for (int i = 0; i < 3000; i++) {
            float b = 0.5f + 0.5f * sinf(i * 0.1f);
            b = powf(b, 2.0f); // emphasize peaks
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
        
        g_audioSamples = dec.getSamples(); // Copy to keep alive
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
    window = SDL_CreateWindow("Visualizer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_RESIZABLE);
#else
    window = SDL_CreateWindow("Visualizer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_RESIZABLE);
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
