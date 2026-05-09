#pragma once

#include <vector>
#include <cstdint>
#include <SDL.h>

class AudioDecoder {
    std::vector<float> samples;
    int sampleRate;
public:
    AudioDecoder();
    bool loadMP3(const uint8_t* data, size_t size);
    const std::vector<float>& getSamples() const { return samples; }
    int getSampleRate() const { return sampleRate; }
};

struct Timeline {
    std::vector<float> gradient; // 0.0-1.0 per frame
    float fps;
};

Timeline analyzeAudio(const std::vector<float>& audio, int sampleRate);

// --- Audio playback ---
struct AudioPlayer {
    std::vector<float> buffer;
    const float* samples = nullptr;
    int totalSamples = 0;
    int currentSample = 0;
    bool started = false;
    int sampleRate = 44100;
    Uint32 lastCallbackTime = 0;
    int lastCallbackSample = 0;
    
    void setSamples(const std::vector<float>& data, int rate);
    void start();
    void stop();
    void fillStream(Uint8* stream, int len);
    float getPlaybackTime() const;
};

// SDL audio callback wrapper
void sdlAudioCallback(void* userdata, Uint8* stream, int len);
