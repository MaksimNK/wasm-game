# Music Visualizer

C++ music visualization system that analyzes MP3 audio in real-time and renders synchronized visual effects. Compiles to both native desktop (SDL2) and WebAssembly (Emscripten) targets.

---

## Quick Start

```bash
# Native build (macOS/Linux with SDL2)
./build.sh
./game audio/test.mp3

# WebAssembly build
./build.sh wasm
./build_and_run.sh          # Builds, starts server, opens browser
```

---

## Project Structure

```
.
├── src/
│   ├── main.cpp           # SDL2 init, render loop, audio playback sync
│   └── audio.cpp          # MP3 decode + FFT analysis pipeline
├── include/
│   └── audio.hpp          # AudioDecoder, Timeline, analyzeAudio() API
├── deps/
│   ├── minimp3.h          # MP3 decoder (CC0)
│   ├── minimp3_ex.h       # MP3 decoder extras
│   ├── kiss_fft.h         # FFT library (BSD)
│   ├── kiss_fft.c
│   ├── _kiss_fft_guts.h
│   └── kiss_fft_log.h     # Logging shim for KissFFT
├── audio/
│   ├── test.mp3           # Default test audio
│   └── 1.opus             # Additional audio file
├── out/
│   ├── index.html         # HTML shell for WASM build
│   ├── game.js            # Generated Emscripten JS glue
│   └── game.wasm          # Generated WebAssembly binary
├── spec/
│   ├── 1-init.md          # Early project notes
│   └── 2-audio.md         # Full audio analysis specification
├── build.sh               # Build script (native or WASM)
├── build_and_run.sh       # Full WASM build + dev server
├── fetch_deps.sh          # Download minimp3 + KissFFT
├── CMakeLists.txt         # CMake configuration (alternative)
└── .vscode/
    └── c_cpp_properties.json
```

---

## Architecture

### Data Flow

```
MP3 File → minimp3 → PCM float samples → KissFFT STFT
                                              ↓
                                         Frequency Bins
                                              ↓
                         Asymmetric Gaussian Weighting (center 7777Hz)
                                              ↓
                                   Energy per Frame
                                              ↓
                              Percentile Normalization (90th percentile)
                                              ↓
                              Noise Gate + Power Curve (peaks only)
                                              ↓
                              Asymmetric Smoothing (fast attack, slow decay)
                                              ↓
                                    Timeline (brightness vector)
                                              ↓
                              SDL2 Renderer (background brightness + tint)
```

### Key Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| FFT Size | 2048 | STFT window length |
| Hop Size | 512 | Frame advance (86 FPS @ 44.1kHz) |
| Gaussian Center | 7777 Hz | Focus frequency for energy extraction |
| Sigma Low | 1800 Hz | Tight below center |
| Sigma High | 3500 Hz | Wide above center |
| Noise Gate | 0.55 | Values below become pure black |
| Power Curve | 0.15 | Emphasizes strong peaks |
| Attack Alpha | 0.9 | Fast rise |
| Decay Alpha | 0.15 | Slow fall |

---

## Current Features

- **MP3 Loading**: Decodes any MP3 to mono float PCM via minimp3
- **Real-time Audio Playback**: SDL2 audio callback with sample-accurate sync
- **FFT Analysis**: STFT with Hann window, asymmetric Gaussian frequency weighting
- **Brightness Visualization**: Fullscreen brightness mapped to music energy with warm/cool tint
- **Cross-platform**: Native desktop (macOS/Linux) and browser (WASM) builds
- **Fallback Mode**: Sine-wave test data when no audio file is provided
- **User Interaction**: Click or key press to start playback

---

## Build System

### Dependencies

- **Native**: SDL2, Clang/Clang++
- **WASM**: Emscripten SDK (`emcc`)
- **Auto-fetched**: minimp3, KissFFT (run `fetch_deps.sh` or let `build.sh` handle it)

### Build Commands

```bash
# Native binary
./build.sh                  # Output: ./game

# WebAssembly
./build.sh wasm             # Output: out/game.js, out/game.wasm

# Full dev workflow (WASM)
./build_and_run.sh          # Build + start server on :6931 + open browser
```

### CMake (Alternative)

```bash
mkdir build && cd build
cmake ..
make
```

---

## Audio Analysis Pipeline

The analysis in `src/audio.cpp:analyzeAudio()` produces a brightness timeline:

1. **Decode**: MP3 → int16 PCM → mono float [-1.0, 1.0]
2. **STFT**: Overlapping Hann windows, 2048-point FFT, 512-sample hop
3. **Weighting**: Asymmetric Gaussian centered at 7777Hz (emphasizes high-mid energy)
4. **Normalize**: 90th percentile scaling to [0, 1]
5. **Gate/Curve**: Hard gate at 0.55, power curve 0.15 (only strong peaks survive)
6. **Smooth**: Asymmetric smoothing (fast attack 0.9, slow decay 0.15)

The renderer uses this timeline to drive fullscreen brightness with a subtle warm tint during peak moments.

---

## Future Work

### Visual Enhancements
- [ ] **Frequency Band Visualization**: Show bass/mid/high as separate bars or rings
- [ ] **Particle Systems**: Spawn particles on beat peaks
- [ ] **Waveform Display**: Raw audio waveform overlay
- [ ] **3D Effects**: Add OpenGL/WebGL shaders for more complex visualizations
- [ ] **Color Palettes**: Map frequency content to hue/saturation instead of just brightness
- [ ] **Beat Detection**: Add explicit beat/onset detection for flash effects
- [ ] **Spectrum Analyzer**: Real-time scrolling spectrogram display

### Audio Features
- [ ] **Streaming Playback**: Support for large files via chunked decoding
- [ ] **Multiple Formats**: Add OGG/Vorbis, FLAC, WAV support
- [ ] **Microphone Input**: Live visualization from microphone capture
- [ ] **Playlist Support**: Queue multiple tracks
- [ ] **Tempo/BPM Detection**: Extract BPM for synchronized effects
- [ ] **Onset Detection**: Use spectral flux for precise beat timing

### Game/Rhythm Elements
- [ ] **Note Highway**: Falling notes aligned to beats (see `spec/2-audio.md`)
- [ ] **Lane System**: 4-lane rhythm game with hit detection
- [ ] **Scoring**: Accuracy-based scoring system
- [ ] **Difficulty Levels**: Easy/Normal/Hard note density
- [ ] **Hold Notes & Slides**: Extended note types
- [ ] **Auto-generation**: Generate note patterns from audio analysis peaks

### Technical Improvements
- [ ] **Configuration File**: JSON/YAML config for analysis parameters
- [ ] **Preset System**: Save/load visualization presets
- [ ] **Export**: Export analyzed timeline to JSON/binary for caching
- [ ] **Performance**: SIMD optimizations for FFT and analysis
- [ ] **Mobile**: Touch controls and responsive layout
- [ ] **CI/CD**: GitHub Actions for automated WASM builds

### Code Quality
- [ ] **Unit Tests**: Add tests for decoder and analysis pipeline
- [ ] **Error Handling**: Better error messages and graceful degradation
- [ ] **Logging**: Structured logging instead of printf
- [ ] **Documentation**: Doxygen or similar API documentation

---

## Specification Reference

See `spec/2-audio.md` for the original full technical specification including:
- Complete module architecture (AudioDecoder, FrequencyAnalyzer, IntensityMapper, GameEventGenerator)
- Algorithm pseudocode for peak detection, smoothing, adaptive thresholds
- Frequency band definitions (Bass 20-250Hz, Mid 250-4kHz, High 4k-20kHz)
- WASM optimization strategies
- Game data format and serialization

---

## License

- **minimp3**: CC0 (Public Domain)
- **KissFFT**: BSD
- **Project code**: Add your license here

---

## Notes

- The asymmetric Gaussian at 7777Hz is tuned for the specific test audio. Adjust `center`, `sigmaLow`, and `sigmaHigh` in `src/audio.cpp:58-60` for different music genres.
- WebAssembly build requires `--preload-file audio` to bundle MP3 files into the virtual filesystem.
- The noise gate at 0.55 means quiet passages show pure black; lower this for more continuous visualization.
