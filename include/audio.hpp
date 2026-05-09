#pragma once

#include <vector>
#include <cstdint>

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
    std::vector<float> brightness; // 0.0-1.0 per frame
    float fps;
};

Timeline analyzeAudio(const std::vector<float>& audio, int sampleRate);
