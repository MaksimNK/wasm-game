# AGENTS.md - Project Context for AI Assistants

## Project Overview

C++ music visualizer that analyzes MP3 audio via FFT and renders a real-time waveform progress bar. Compiles to both native desktop (SDL2) and WebAssembly (Emscripten).

**Key Features:**
- MP3 decoding with minimp3
- FFT frequency analysis with KissFFT
- Fisheye-distorted waveform display
- White-on-black minimal UI with opacity
- Jitter effect for organic texture
- Sample-accurate audio playback sync

---

## Architecture

```
src/main.cpp      - SDL2 init, render loop, audio callback
src/audio.cpp     - MP3 decode + FFT analysis pipeline
include/audio.hpp - AudioDecoder, Timeline structs
```

### Data Flow
```
MP3 → minimp3 → PCM float → KissFFT STFT → brightness vector → SDL2 renderer
```

### Key Parameters (Current)
| Parameter | Value | Description |
|-----------|-------|-------------|
| FFT Size | 2048 | STFT window |
| Hop Size | 512 | Frame advance (86 FPS @ 44.1kHz) |
| Gaussian Center | 7777 Hz | Focus frequency for energy |
| Noise Gate | 0.55 | Below = pure black |
| Power Curve | 0.15 | Emphasizes strong peaks |
| Window Seconds | 0.77s | Displayed time range |
| Playhead Ratio | 0.21 | 21% from left edge |
| Fisheye Distortion | 6.0 | Aggressive cubic curve |
| Jitter | ±20% | Random height variation |
| Opacity Quiet | 0-15 | Near-invisible for low energy |
| Opacity Loud | 15-255 | Full range for high energy |

---

## Build System

```bash
# Native (macOS/Linux with SDL2)
./build.sh

# WebAssembly (requires Emscripten)
./build.sh wasm
./build_and_run.sh  # Build + server + browser
```

**Dependencies:**
- Native: SDL2, Clang/Clang++
- Auto-fetched: minimp3, KissFFT (via `fetch_deps.sh`)

---

## Code Conventions

### Style
- C++11 standard
- Snake_case for locals, camelCase for functions
- Global state prefixed with `g_`
- No classes (structs only)

### SDL2 Patterns
```cpp
// Always check renderer size with fallback
int w = 800, h = 600;
SDL_GetRendererOutputSize(renderer, &w, &h);
if (w <= 0 || h <= 0) SDL_GetWindowSize(window, &w, &h);

// Enable alpha blending before transparent drawing
SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
```

### Audio Callback
```cpp
void audioCallback(void*, Uint8* stream, int len) {
    // g_audio.currentSample updated here (chunked, ~93ms intervals)
    // Visuals use SDL_GetTicks() for smooth animation, NOT currentSample
}
```

---

## Visual System

### Render Loop (`main.cpp:render()`)
1. Clear to black
2. Enable alpha blending
3. Calculate fisheye-distorted time for each pixel
4. Draw waveform bars with jitter
5. Draw center line (30% opacity)
6. Draw playhead line (100% opacity)

### Fisheye Formula
```cpp
f(x) = x * (1 + a*x^2) / (1 + a)  // a=6.0
// f'(0) = 0.14 (7x zoom center)
// f'(1) = 2.71 (compressed edges)
```

### Opacity Mapping
```cpp
if (b < 0.3) alpha = b / 0.3 * 15;      // 0-15
else         alpha = 15 + (b-0.3)/0.7 * 240;  // 15-255
```

---

## Common Changes

### Adjust bar size
Edit `BAR_W` and `BAR_H` constants in `render()`.

### Change fisheye strength
Modify `DISTORTION` value in distort lambda (line ~119).

### Adjust opacity curve
Modify the alpha calculation in the pixel loop.

### Change jitter amount
Modify `0.8f + (rand() / RAND_MAX) * 0.4f` for different ranges.

---

## Known Limitations

1. **Audio-visual drift possible**: Timer-based visuals vs audio callback. Not noticeable for short tracks.
2. **No seek/scrub**: Cannot jump to arbitrary position.
3. **Single audio format**: Only MP3 supported.
4. **No pause**: Only play/stop.

---

## File Structure

```
src/
  main.cpp       - Entry point, SDL loop, renderer
  audio.cpp      - Decoder, FFT, analysis pipeline
include/
  audio.hpp      - AudioDecoder, Timeline structs
deps/
  minimp3.h      - MP3 decoder (auto-fetched)
  minimp3_ex.h
  kiss_fft.h     - FFT library (auto-fetched)
  kiss_fft.c
out/
  index.html     - HTML shell for WASM
  game.js        - Generated JS glue
  game.wasm      - Generated binary
audio/
  test.mp3       - Default test audio
build.sh         - Build script (native/wasm)
build_and_run.sh - Full dev workflow
fetch_deps.sh    - Download dependencies
```

---

## Testing Checklist

When making changes, verify:
- [ ] `./build.sh` compiles native binary
- [ ] `./build.sh wasm` compiles WASM
- [ ] Native: `./game audio/test.mp3` runs
- [ ] WASM: `./build_and_run.sh` opens browser
- [ ] Visuals appear on black background
- [ ] Playhead moves smoothly
- [ ] Waveform responds to audio
- [ ] No console errors in browser dev tools

---

## Dependencies URLs

- minimp3: https://github.com/lieff/minimp3 (CC0)
- KissFFT: https://github.com/mborgerding/kissfft (BSD)
- SDL2: https://libsdl.org/
- Emscripten: https://emscripten.org/
