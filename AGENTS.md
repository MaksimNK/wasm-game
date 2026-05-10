# AGENTS.md - Project Context for AI Assistants

## Project Overview

C++ rhythm action game with music-reactive visuals. Player auto-jumps between enemies with sword slashes timed to music beats. Compiles to native desktop (SDL2) and WebAssembly (Emscripten).

**Key Features:**
- MP3 decoding with minimp3
- FFT frequency analysis with KissFFT
- Rhythm-reactive gameplay (enemy speed, spawn rate, time dilation)
- Sword combat with windup/slash animation
- Blow-away physics on enemy death
- White-on-black minimal UI
- Fisheye-distorted progress bar
- Sample-accurate audio playback sync

---

## Architecture

```
src/main.cpp      - SDL2 init, render loop, audio callback
src/audio.cpp     - MP3 decode + FFT analysis pipeline
src/game.cpp      - Game state management, input handling
src/render.cpp    - Waveform rendering, fisheye distortion, visual effects
include/audio.hpp - AudioDecoder, Timeline structs
include/game.hpp  - Game state, input enums
include/render.hpp - Render constants, draw helpers
```

### Data Flow
```
MP3 → AudioDecoder → PCM float → KissFFT → Timeline.gradient
                                            ↓
Timeline → updateGame() → GameState (raw positions, alive flags)
                              ↓
                    updateAnimations() → smooth angles, camera, ribbons
                              ↓
                        renderFrame() → SDL2
```

### Key Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| FFT Size | 2048 | STFT window (~46ms @ 44.1kHz) |
| Hop Size | 512 | Frame advance (~12ms, 86 FPS) |
| Freq Center | 7777 Hz | Gaussian focus for energy |
| Noise Gate | 0.55 | Below = pure black |
| Power Curve | 0.15 | Emphasizes strong peaks |
| Jump Duration | 0.25s | Attack lunge time |
| Window Seconds | 0.77s | Progress bar time range |
| Playhead Ratio | 0.21 | 21% from left edge |
| Fisheye Distortion | 6.0 | Aggressive cubic curve |
| Camera Follow | 5.0 | Pos smoothing speed |
| Camera Zoom Speed | 4.0 | Zoom smoothing speed |
| Sword Windup | -2.79 rad | Backswing angle |
| Slash End | 2.10 rad | Follow-through angle |

---

## Build System (Devbox)

This project uses [Devbox](https://www.jetpack.io/devbox) for reproducible development environments. All dependencies are managed through `devbox.json`.

```bash
# Enter the dev environment
devbox shell

# Or run commands directly
devbox run <script>
```

**Available Scripts:**

| Command | Description |
|---------|-------------|
| `devbox run fetch-deps` | Download minimp3 + KissFFT into `deps/` |
| `devbox run build-native` | Compile desktop binary with clang++ |
| `devbox run build-wasm` | Compile WebAssembly with emcc |
| `devbox run build-and-run` | Full wasm workflow: clean → build → serve → open browser |
| `devbox run clean` | Remove build artifacts |
| `devbox run run` | Build (if needed) and run native binary |

**Example workflows:**
```bash
# Native development
devbox run build-native
devbox run run

# WebAssembly development
devbox run build-and-run
```

**Managed Dependencies:**
- `clang` — C++ compiler
- `SDL2` — Graphics/audio library
- `emscripten` — WebAssembly toolchain
- `python3` — Built-in HTTP server
- `curl` — Download dependencies
- `lsof` — Port management

---

## Code Conventions

### Style
- C++11 standard
- Snake_case for locals, camelCase for functions
- Global state prefixed with `g_`
- Structs only (no classes except AudioDecoder)

### Game Module Rules
- **Never include SDL.h**
- Never interpolate visuals (raw positions only)
- Never access `swordOffset` or `camera` smoothing
- Combat: set `hasSlashed = true`, let render handle transitions

### Render Module Rules
- **Never change game rules or spawn logic**
- `updateAnimations()` runs after `updateGame()` in main loop
- Camera smoothing done here (game sets raw target)
- All drawing primitives stay in this module

### SDL2 Patterns (render.cpp only)
```cpp
// Renderer size with fallback
int w, h;
SDL_GetRendererOutputSize(renderer, &w, &h);
if (w <= 0 || h <= 0) SDL_GetWindowSize(window, &w, &h);

// Enable alpha blending before transparent drawing
SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
```

### Audio Callback
```cpp
void sdlAudioCallback(void* userdata, Uint8* stream, int len) {
    AudioPlayer* player = static_cast<AudioPlayer*>(userdata);
    player->fillStream(stream, len);
}
```

---

## Visual System (render.cpp)

### Frame Pipeline
1. Clear to black
2. Enable alpha blending
3. Draw sword ribbon trail (quad fill)
4. Draw enemies (circles with inner detail)
5. Draw player triangle
6. Draw sword
7. Draw progress bar with fisheye distortion

### Animation System
```cpp
// Called every frame AFTER updateGame()
updateAnimations(game, dt);
```

- **Sword**: smooth interpolation between windup/slash/idle states
- **Player angle**: rotates toward movement direction (12 rad/s)
- **Camera**: smooth follow (5.0 speed) + zoom lerp (4.0 speed)
- **Ribbons**: generated from sword tip, fade over lifetime

### Fisheye Formula (progress bar)
```cpp
f(x) = x * (1 + a*x^2) / (1 + a)  // a=6.0
// f'(0) = 0.14 (7x zoom center)
// f'(1) = 2.71 (compressed edges)
```

### Opacity Mapping
```cpp
if (g < 0.3) alpha = g / 0.1 * 15;       // 0-15
else          alpha = 15 + (g-0.3)/0.7 * 240;  // 15-255
```

---

## Game System (game.cpp)

### Core Loop
```cpp
updateGame(game, timeline, realDt, musicTime):
  1. Get gradient at musicTime
  2. Scale dt by rhythm intensity
  3. Spawn enemies to target count (3-5 based on gradient)
  4. Process jump physics (bezier curve)
  5. Detect slash hits (sword tip vs enemy radius)
  6. Update enemy AI (chase player, blow away on death)
  7. Remove dead enemies after flash timer
  8. Update camera (raw position = player.pos)
```

### Attack Flow
```cpp
processAttack(game, timeline):
  1. Find nearest alive enemy (> 40px away)
  2. Set player.jumping = true
  3. Calculate bezier curve to enemy
  4. On approach (< 70px): game sets hasSlashed = true
  5. On slash: kill enemies in range, trigger blowAwayEnemies()
```

---

## Common Changes

### Adjust sword animation
Edit constants in `render.cpp`: `SWORD_WINDUP_ANGLE`, `SWORD_SLASH_SPEED`, etc.

### Change enemy spawn rate
Edit `BASE_ENEMY_COUNT` or `ENEMY_COUNT_GRADIENT_SCALE` in `game.cpp`.

### Adjust progress bar
Edit `BAR_W`, `BAR_H`, `DISTORTION_FACTOR` in `render.cpp`.

### Modify combat range
Edit `SWORD_HIT_EXTRA_RADIUS` or `SLASH_TRIGGER_DIST` in `game.cpp`.

### Change camera behavior
Edit `CAMERA_FOLLOW_SPEED` or `CAMERA_ZOOM_SPEED` in `render.cpp`.

---

## Known Limitations

1. **Audio-visual drift possible**: Timer-based visuals vs audio callback
2. **No seek/scrub**: Cannot jump to arbitrary position
3. **Single audio format**: Only MP3 supported
4. **No pause**: Only play/stop
5. **Game/render boundary**: `hasSlashed` flag shared for combat timing

---

## File Structure

```
src/
  main.cpp       - Entry point, SDL loop, renderer
  audio.cpp      - Decoder, FFT, analysis pipeline
  game.cpp       - Game state management, input handling
  render.cpp     - Waveform rendering, fisheye distortion, visual effects
include/
  audio.hpp      - AudioDecoder, Timeline structs
  game.hpp       - Game state, input enums
  render.hpp     - Render constants, draw helpers
deps/
  minimp3.h       - MP3 decoder (auto-fetched)
  minimp3_ex.h
  kiss_fft.h      - FFT library (auto-fetched)
  kiss_fft.c
  _kiss_fft_guts.h
  kiss_fft_log.h
out/
  index.html     - HTML shell for WASM
  game.js        - Generated JS glue
  game.wasm      - Generated binary
  game.data      - Preloaded audio assets
audio/
  test.mp3       - Default test audio
  test-1.mp3     - Additional test audio
devbox.json      - Devbox configuration (dependencies + scripts)
devbox.lock      - Lock file for reproducible builds
```

---

## Testing Checklist

When making changes, verify:
- [ ] `devbox run build-native` compiles native binary
- [ ] `devbox run build-wasm` compiles WASM
- [ ] Native: `devbox run run` executes successfully
- [ ] WASM: `devbox run build-and-run` opens browser
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
