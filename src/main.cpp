#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <SDL.h>
#include "audio.hpp"
#include "game.hpp"
#include "render.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// --- App constants ---
static constexpr const char* APP_NAME = "Rhythm Slayer";
static constexpr const char* DEFAULT_AUDIO_PATH = "audio/test-1.mp3";
static constexpr int DEFAULT_WIDTH = 800;
static constexpr int DEFAULT_HEIGHT = 600;

// --- Audio constants ---
static constexpr int AUDIO_SAMPLES = 4096;
static constexpr SDL_AudioFormat AUDIO_FORMAT = AUDIO_F32SYS;
static constexpr int AUDIO_CHANNELS = 1;

// --- State ---
static Timeline g_timeline;
static GameState g_game;
static AudioPlayer g_audioPlayer;
static Renderer* g_renderer = nullptr;
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

static bool step() {
    bool attack = false;
    bool start = false;
    InputState input;
    
    if (!pollEvents(g_game, g_timeline, attack, start, input)) {
        return false;
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
    
    // Process attack input after game update so enemy deaths are synced
    if (attack) {
        if (!g_running) {
            startAudio();
            initGame(g_game, DEFAULT_WIDTH, DEFAULT_HEIGHT);
        } else {
            bool goodTiming = isGoodTiming(g_timeline, g_time);
            processScoreHit(g_game, goodTiming);
            processAttack(g_game, g_timeline, input);
        }
    }
    
    // Animation pass (render team)
    updateAnimations(g_game, realDt);
    
    renderFrame(g_renderer, g_game, g_timeline, g_time, g_running);
    return true;
}

#ifdef __EMSCRIPTEN__
static void emMainLoop() {
    if (!step()) {
        emscripten_cancel_main_loop();
        destroyRenderer(g_renderer);
        SDL_CloseAudio();
        SDL_Quit();
    }
}
#endif

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
    
    g_renderer = createRenderer(APP_NAME, DEFAULT_WIDTH, DEFAULT_HEIGHT);
    if (!isRendererValid(g_renderer)) {
        fprintf(stderr, "[ERROR] Failed to create renderer\n");
        SDL_Quit();
        return 1;
    }
    
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
    emscripten_set_main_loop(emMainLoop, 0, 1);
#else
    while (step()) {}
    destroyRenderer(g_renderer);
    SDL_CloseAudio();
    SDL_Quit();
#endif
    
    return 0;
}
