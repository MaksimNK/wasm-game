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
};
static AudioState g_audio = {nullptr, 0, 0, false, 44100};
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
    g_start = SDL_GetTicks();
    if (g_audio.samples && g_audio.totalSamples > 0) {
        SDL_PauseAudio(0);
    }
}

static void render() {
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    
    float b = 0;
    if (g_running && !g_timeline.brightness.empty()) {
        int f = (int)(g_time * g_timeline.fps);
        if (f >= 0 && f < (int)g_timeline.brightness.size()) {
            b = g_timeline.brightness[f];
        }
    }
    
    // Brightness from black to white based on music energy
    Uint8 v = (Uint8)(b * 255);
    SDL_SetRenderDrawColor(renderer, v, v, v, 255);
    SDL_RenderClear(renderer);
    
    // Optional: subtle tint - warm when bright, cool when dark
    if (b > 0.3f) {
        int tint = (int)((b - 0.3f) * 60);
        SDL_SetRenderDrawColor(renderer, v, v - tint/3, v - tint/2, 255);
        SDL_RenderClear(renderer);
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
            // Sample-accurate sync to audio playback
            g_time = (float)g_audio.currentSample / g_audio.sampleRate;
        } else {
            // Test data: use timer
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
    
    window = SDL_CreateWindow("Visualizer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_RESIZABLE);
    if (!window) { SDL_Quit(); return 1; }
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
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
