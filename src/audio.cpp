#include "audio.hpp"
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
#include "kiss_fft.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AudioDecoder::AudioDecoder() : sampleRate(44100) {}

bool AudioDecoder::loadMP3(const uint8_t* data, size_t size) {
    mp3dec_t mp3d;
    mp3dec_file_info_t info;
    mp3dec_init(&mp3d);
    if (mp3dec_load_buf(&mp3d, data, size, &info, NULL, NULL)) return false;
    
    sampleRate = info.hz;
    int totalSamples = info.samples / info.channels;
    samples.resize(totalSamples);
    
    if (info.channels == 1) {
        for (int i = 0; i < totalSamples; i++) samples[i] = info.buffer[i] / 32768.0f;
    } else {
        for (int i = 0; i < totalSamples; i++) {
            samples[i] = (info.buffer[i*2] + info.buffer[i*2+1]) / 65536.0f;
        }
    }
    free(info.buffer);
    return true;
}

static float hann(int i, int n) {
    return 0.5f * (1.0f - cosf(2.0f * M_PI * i / (n - 1)));
}

Timeline analyzeAudio(const std::vector<float>& audio, int sampleRate) {
    const int FFT = 2048;
    const int HOP = 512;
    
    Timeline t;
    t.fps = (float)sampleRate / HOP;
    
    int frames = (int)audio.size() > FFT ? ((int)audio.size() - FFT) / HOP + 1 : 0;
    if (frames <= 0) return t;
    
    t.brightness.resize(frames);
    
    kiss_fft_cfg cfg = kiss_fft_alloc(FFT, 0, NULL, NULL);
    if (!cfg) return t;
    
    std::vector<kiss_fft_cpx> in(FFT), out(FFT);
    int bins = FFT / 2 + 1;
    
    // Asymmetric Gaussian: narrow below peak, wide above
    float center = 7777.0f;
    float sigmaLow = 1800.0f;   // Tight below 7777Hz
    float sigmaHigh = 3500.0f;  // Wide above 7777Hz
    
    for (int f = 0; f < frames; f++) {
        int start = f * HOP;
        for (int i = 0; i < FFT; i++) {
            float s = (start + i < (int)audio.size()) ? audio[start + i] : 0.0f;
            in[i].r = s * hann(i, FFT);
            in[i].i = 0;
        }
        
        kiss_fft(cfg, in.data(), out.data());
        
        // Asymmetric Gaussian centered at 7777Hz
        float sum = 0;
        float weightSum = 0;
        for (int b = 0; b < bins; b++) {
            float re = out[b].r, im = out[b].i;
            float mag = sqrtf(re*re + im*im);
            float freq = (float)b * sampleRate / FFT;
            float diff = freq - center;
            float sigma = (freq < center) ? sigmaLow : sigmaHigh;
            float w = expf(-diff * diff / (2.0f * sigma * sigma));
            sum += mag * w;
            weightSum += w;
        }
        t.brightness[f] = weightSum > 0 ? sum / weightSum : 0;
    }
    
    kiss_fft_free(cfg);
    
    // Percentile normalization (90th percentile)
    std::vector<float> sorted = t.brightness;
    std::sort(sorted.begin(), sorted.end());
    float p90 = sorted[(int)(sorted.size() * 0.90f)];
    float norm = p90 > 0.001f ? p90 : 1.0f;
    for (float& v : t.brightness) {
        v = std::min(1.0f, v / norm);
    }
    
    // Aggressive contrast: noise gate + steep curve for peaks only
    for (float& v : t.brightness) {
        // Noise gate: below 0.55 becomes pure black
        if (v < 0.55f) {
            v = 0.0f;
        } else {
            // Remap 0.55-1.0 to 0.0-1.0, then steep curve
            v = (v - 0.55f) / 0.45f;
            // Power curve: only strong peaks survive
            // v^0.15: 0.5->0.90, 0.7->0.94, 0.8->0.96, 0.9->0.97
            v = powf(v, 0.15f);
        }
    }
    
    // Asymmetric smoothing: fast attack, slow decay
    std::vector<float> smoothed(t.brightness.size());
    smoothed[0] = t.brightness[0];
    for (size_t i = 1; i < t.brightness.size(); i++) {
        float alpha = (t.brightness[i] > smoothed[i-1]) ? 0.9f : 0.15f;
        smoothed[i] = alpha * t.brightness[i] + (1.0f - alpha) * smoothed[i-1];
    }
    t.brightness = smoothed;
    
    return t;
}
