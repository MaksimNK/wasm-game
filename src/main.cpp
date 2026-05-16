#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <SDL.h>
#include <emscripten.h>
#include "audio.hpp"
#include "systems.hpp"
#include "render.hpp"

static constexpr const char* APP_NAME = "Rhythm Slayer";
static constexpr const char* DEFAULT_AUDIO_PATH = "audio/test-4.mp3";
static constexpr int AUDIO_SAMPLES = 4096;
static constexpr SDL_AudioFormat AUDIO_FORMAT = AUDIO_F32SYS;
static constexpr int AUDIO_CHANNELS = 2;

static Timeline g_timeline;
static GameState g_game;
static AudioPlayer g_audio_player;
static Renderer* g_renderer = nullptr;
static EventBus g_events;
static VisualFrame g_visual_frame;
static float g_time = 0.0f;
static bool g_running = false;
static bool g_has_audio = false;
static Uint32 g_start = 0;

static std::vector<uint8_t> load_file(const char* path) {
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

static void start_audio() {
    if (g_running) return;
    g_running = true;
    g_audio_player.start();
}

static bool step() {
    if (!Systems::poll_input(g_events)) return false;
    
    Uint32 now = SDL_GetTicks();
    static Uint32 last_time = now;
    float real_dt = (now - last_time) / 1000.0f;
    last_time = now;
    if (real_dt > 0.1f) real_dt = 0.1f;
    
    // Start on first input
    if (!g_running && !g_events.attacks.empty()) {
        start_audio();
        g_game.init();
        g_events.clear();
    }
    
    if (g_running) {
        g_time = g_has_audio ? g_audio_player.getPlaybackTime() : (SDL_GetTicks() - g_start) / 1000.0f;
        
        float gradient = get_brightness_at_time(g_timeline, g_time);
        g_game.time_scale = get_time_scale(gradient);
        float dt = real_dt * g_game.time_scale;
        g_game.music_time = g_time;
        g_game.game_time += dt;
        
        Systems::update_player_flow(g_game, g_events, dt, g_timeline, g_time, gradient);
        Systems::update_enemies(g_game, dt);
        Systems::update_spawn(g_game, dt, gradient);
        Systems::update_score(g_game, g_events, dt, gradient);
        Systems::build_visual_frame(g_game, dt, g_visual_frame);
        Systems::update_camera(g_game.camera, g_game.player.pos, 
                                g_game.player.trajectory.active, dt);
        
        g_events.clear();
    }
    
    render_frame(g_renderer, g_visual_frame, g_timeline, g_time, g_running);
    return true;
}

static void em_main_loop() {
    if (!step()) {
        emscripten_cancel_main_loop();
        destroy_renderer(g_renderer);
        SDL_CloseAudio();
        SDL_Quit();
    }
}

int main(int argc, char* argv[]) {
    const char* path = argc > 1 ? argv[1] : DEFAULT_AUDIO_PATH;
    
    auto mp3 = load_file(path);
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
    g_audio_player.setSamples(dec.getSamples(), dec.getSampleRate());
    g_has_audio = true;
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "[ERROR] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    
    g_renderer = create_renderer(APP_NAME);
    if (!is_renderer_valid(g_renderer)) {
        fprintf(stderr, "[ERROR] Failed to create renderer\n");
        SDL_Quit();
        return 1;
    }
    
    g_game.init();
    
    if (g_audio_player.samples && g_audio_player.totalSamples > 0) {
        SDL_AudioSpec want, have;
        SDL_memset(&want, 0, sizeof(want));
        want.freq = g_audio_player.sampleRate;
        want.format = AUDIO_FORMAT;
        want.channels = AUDIO_CHANNELS;
        want.samples = AUDIO_SAMPLES;
        want.callback = sdlAudioCallback;
        want.userdata = &g_audio_player;
        if (SDL_OpenAudio(&want, &have) >= 0) {
            printf("[INFO] Audio ready: %d Hz\n", g_audio_player.sampleRate);
        } else {
            fprintf(stderr, "[WARN] Failed to open audio: %s\n", SDL_GetError());
        }
    }
    
    g_start = SDL_GetTicks();
    emscripten_set_main_loop(em_main_loop, 0, 1);
    return 0;
}
