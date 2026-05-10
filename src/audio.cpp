#include "audio.hpp"
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"
#include "kiss_fft.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Audio constants ---
static constexpr float INT16_MAX_F = 32768.0f;
static constexpr float INT16_STEREO_DIVISOR = 65536.0f;

// --- FFT constants ---
static constexpr int FFT_SIZE = 2048;    // STFT window size (~46ms @ 44.1kHz)
static constexpr int HOP_SIZE = 512;     // Frame advance (~12ms, 86 FPS @ 44.1kHz)

// --- Frequency focus constants ---
static constexpr float FREQ_CENTER = 7777.0f;      // Focus frequency for energy
static constexpr float SIGMA_LOW = 1800.0f;        // Tight below center
static constexpr float SIGMA_HIGH = 3500.0f;       // Wide above center

// --- Normalization constants ---
static constexpr float NORMALIZATION_PERCENTILE = 0.90f;
static constexpr float MIN_NORM_THRESHOLD = 0.001f;

// --- Contrast constants ---
static constexpr float NOISE_GATE = 0.55f;
static constexpr float POWER_CURVE = 0.15f;

// --- Smoothing constants ---
static constexpr float ATTACK_ALPHA = 0.90f;   // Fast rise
static constexpr float DECAY_ALPHA = 0.15f;    // Slow fall

// Constructor is defaulted in header

bool AudioDecoder::loadMP3(const uint8_t* data, size_t size) {
    mp3dec_t mp3d;
    mp3dec_file_info_t info;
    mp3dec_init(&mp3d);
    if (mp3dec_load_buf(&mp3d, data, size, &info, NULL, NULL)) return false;
    
    sampleRate = info.hz;
    int totalSamples = info.samples / info.channels;
    samples.resize(totalSamples);
    
    if (info.channels == 1) {
        for (int i = 0; i < totalSamples; i++) {
            samples[i] = info.buffer[i] / INT16_MAX_F;
        }
    } else {
        for (int i = 0; i < totalSamples; i++) {
            float left = info.buffer[i * 2];
            float right = info.buffer[i * 2 + 1];
            samples[i] = (left + right) / INT16_STEREO_DIVISOR;
        }
    }
    free(info.buffer);
    return true;
}

// Hann window: tapers signal to zero at edges to reduce spectral leakage
// Returns weight in [0,1] for sample i out of n total samples
static float hann(int i, int n) {
    return 0.5f * (1.0f - cosf(2.0f * M_PI * i / (n - 1)));
}

Timeline analyzeAudio(const std::vector<float>& audio, int sampleRate) {
    Timeline t;
    t.fps = (float)sampleRate / HOP_SIZE;
    
    int frames = (int)audio.size() > FFT_SIZE
        ? ((int)audio.size() - FFT_SIZE) / HOP_SIZE + 1
        : 0;
    if (frames <= 0) return t;
    
    t.gradient.resize(frames);
    
    kiss_fft_cfg cfg = kiss_fft_alloc(FFT_SIZE, 0, NULL, NULL);
    if (!cfg) return t;
    
    std::vector<kiss_fft_cpx> in(FFT_SIZE), out(FFT_SIZE);
    int bins = FFT_SIZE / 2 + 1;
    
    for (int f = 0; f < frames; f++) {
        int start = f * HOP_SIZE;
        for (int i = 0; i < FFT_SIZE; i++) {
            float s = (start + i < (int)audio.size()) ? audio[start + i] : 0.0f;
            in[i].r = s * hann(i, FFT_SIZE);
            in[i].i = 0;
        }
        
        kiss_fft(cfg, in.data(), out.data());
        
        // Asymmetric Gaussian centered at FREQ_CENTER
        float sum = 0;
        float weightSum = 0;
        for (int b = 0; b < bins; b++) {
            float re = out[b].r, im = out[b].i;
            float mag = sqrtf(re*re + im*im);
            float freq = (float)b * sampleRate / FFT_SIZE;
            float diff = freq - FREQ_CENTER;
            float sigma = (freq < FREQ_CENTER) ? SIGMA_LOW : SIGMA_HIGH;
            float w = expf(-diff * diff / (2.0f * sigma * sigma));
            sum += mag * w;
            weightSum += w;
        }
        t.gradient[f] = weightSum > 0 ? sum / weightSum : 0;
    }
    
    kiss_fft_free(cfg);
    
    // Percentile normalization
    std::vector<float> sorted = t.gradient;
    std::sort(sorted.begin(), sorted.end());
    float p90 = sorted[(int)(sorted.size() * NORMALIZATION_PERCENTILE)];
    float norm = p90 > MIN_NORM_THRESHOLD ? p90 : 1.0f;
    for (float& v : t.gradient) {
        v = std::min(1.0f, v / norm);
    }
    
    // Noise gate + power curve
    for (float& v : t.gradient) {
        if (v < NOISE_GATE) {
            v = 0.0f;
        } else {
            v = (v - NOISE_GATE) / (1.0f - NOISE_GATE);
            v = powf(v, POWER_CURVE);
        }
    }
    
    // Asymmetric smoothing: fast attack, slow decay
    std::vector<float> smoothed(t.gradient.size());
    smoothed[0] = t.gradient[0];
    for (size_t i = 1; i < t.gradient.size(); i++) {
        float alpha = (t.gradient[i] > smoothed[i-1]) ? ATTACK_ALPHA : DECAY_ALPHA;
        smoothed[i] = alpha * t.gradient[i] + (1.0f - alpha) * smoothed[i-1];
    }
    t.gradient = smoothed;
    
    return t;
}

// --- Audio playback implementation ---

void AudioPlayer::setSamples(const std::vector<float>& data, int rate) {
    buffer = data;
    samples = buffer.data();
    totalSamples = buffer.size();
    sampleRate = rate;
    currentSample = 0;
    started = false;
    lastCallbackTime = 0;
    lastCallbackSample = 0;
}

void AudioPlayer::start() {
    started = true;
    lastCallbackTime = SDL_GetTicks();
    lastCallbackSample = 0;
    SDL_PauseAudio(0);
}

void AudioPlayer::stop() {
    started = false;
    SDL_PauseAudio(1);
}

void AudioPlayer::fillStream(Uint8* stream, int len) {
    if (!started || !samples || currentSample >= totalSamples) {
        memset(stream, 0, len);
        return;
    }
    int n = len / sizeof(float);
    int rem = totalSamples - currentSample;
    int copy = std::min(n, rem);
    memcpy(stream, &samples[currentSample], copy * sizeof(float));
    if (copy < n) memset(stream + copy * sizeof(float), 0, (n - copy) * sizeof(float));
    currentSample += copy;
    lastCallbackTime = SDL_GetTicks();
    lastCallbackSample = currentSample;
}

float AudioPlayer::getPlaybackTime() const {
    if (!started || sampleRate <= 0) return 0.0f;
    float audioTime = (float)lastCallbackSample / sampleRate;
    float elapsedSinceCallback = (SDL_GetTicks() - lastCallbackTime) / 1000.0f;
    return audioTime + elapsedSinceCallback;
}

void sdlAudioCallback(void* userdata, Uint8* stream, int len) {
    AudioPlayer* player = static_cast<AudioPlayer*>(userdata);
    if (player) player->fillStream(stream, len);
}
