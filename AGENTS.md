# AGENTS.md - Project Context for AI Assistants

## Project Overview

C++ rhythm action game with music-reactive visuals. Player auto-jumps between enemies with sword slashes timed to music beats. WebAssembly only (Emscripten + SDL2 + OpenGLES 2.0).

**Key Features:**
- MP3 decoding with minimp3
- FFT frequency analysis with KissFFT
- Timeline gradient drives all gameplay (spawn rate, enemy speed, time dilation)
- Systems + Events architecture (no God Objects)
- Sword combat with windup/slash animation and ribbon trails
- Blow-away physics on enemy death
- White-on-black minimal UI with fisheye progress bar
- Sample-accurate audio playback sync

---

## Architecture: Systems + Events

The game uses a **Systems + EventBus** architecture. Each system has one responsibility. Systems communicate through events, not direct calls.

```
InputSystem ──events──┐
                      ▼
            ┌─────────────────┐
            │    EventBus     │
            └─────────────────┘
                      │
    ┌─────────┬───────┴───────┬──────────┐
    ▼         ▼               ▼          ▼
CombatSystem  MovementSystem  SpawnSystem  ScoreSystem
    │         │               │            │
    └─────────┴───────┬───────┴────────────┘
                      ▼
              AnimationSystem
              (builds VisualFrame)
                      │
                      ▼
              RenderSystem (OpenGL)
```

### File Structure

```
include/
  types.hpp        - Vec2, math helpers, easing functions
  components.hpp   - Pure data structs: Player, Enemy, Camera, ScoreData, SwordRibbon
  events.hpp       - EventBus with AttackEvent, ScoreEvent
  systems.hpp      - GameState, VisualFrame, all system declarations
  render.hpp       - Renderer API (creates OpenGL context, draws VisualFrame)
  audio.hpp        - AudioDecoder, Timeline, AudioPlayer

src/
  main.cpp         - Entry point, orchestrates systems in order
  audio.cpp        - MP3 decode, FFT analysis, audio playback
  components.cpp   - Component reset() implementations
  events.cpp       - EventBus::clear()
  systems.cpp      - ALL gameplay logic (combat, movement, spawn, score, animation)
  render.cpp       - OpenGLES 2.0 renderer (shaders, batching, drawing)

deps/
  minimp3.h, minimp3_ex.h  - MP3 decoder (auto-fetched)
  kiss_fft.h, kiss_fft.c   - FFT library (auto-fetched)

static/
  index.html       - HTML shell for WASM

out/               - Build output (game.js, game.wasm, game.data, index.html)
```

### Data Flow

```
MP3 → AudioDecoder → PCM float → KissFFT → Timeline.gradient
                                              │
                                              ▼
Timeline.gradient → get_brightness_at_time() → gradient value
                                              │
    ┌─────────────────────────────────────────┼──────────────────────────────┐
    │                                         │                              │
    ▼                                         ▼                              ▼
CombatSystem                           SpawnSystem                    MovementSystem
(timing checks)                        (enemy count/speed)            (enemy velocity)
    │                                         │                              │
    ▼                                         ▼                              ▼
EventBus (ScoreEvent)                  Enemy spawning                 Enemy positions
    │                                         │                              │
    └─────────────────────────────────────────┴──────────────────────────────┘
                                              │
                                              ▼
                                       GameState (raw data)
                                              │
                                              ▼
                                       build_visual_frame()
                                              │
                                              ▼
                                       VisualFrame (interpolated visuals)
                                              │
                                              ▼
                                       render_frame() → OpenGL
```

---

## Key Principles

### 1. Components Are Pure Data
**NEVER put behavior in components.** Only structs with fields and `reset()`.

```cpp
// GOOD
struct Player {
    Vec2 pos;
    float angle = 0;
    EntityState state = EntityState::Idle;
    void reset();  // Only this method allowed
};

// BAD - never do this
struct Player {
    void update(float dt);  // NO!
    void attack();          // NO!
};
```

### 2. Systems Own Behavior
Put all gameplay logic in the `Systems` namespace. Each system has one job:

| System | Responsibility | Reads | Emits |
|--------|---------------|-------|-------|
| `poll_input` | Read SDL events | SDL | `AttackEvent` |
| `update_combat` | Attack state machine, hit detection | `GameState`, `Timeline` | `ScoreEvent` |
| `update_movement` | Physics, bezier jumps, enemy chase | `GameState` | — |
| `update_spawn` | Enemy creation | `GameState`, gradient | — |
| `update_score` | Points, combo, levels | `ScoreEvent` | — |
| `build_visual_frame` | Produce VisualFrame from GameState | `GameState` | — |
| `update_camera` | Smooth follow/zoom | `GameState` | — |

### 3. EventBus Is the Glue
Systems communicate **only** through events. No system calls another system directly.

```cpp
// GOOD: Combat emits score event
EventBus::ScoreEvent evt;
evt.good_hit = is_good_timing(timeline, music_time);
evt.enemy_count = 1;
events.scores.push_back(evt);

// BAD: Combat directly modifies score
// game.score.points += 100;  // NEVER do this
```

### 4. VisualFrame Separates Render from Game State
Render **never** reads `GameState`. It only draws `VisualFrame`.

```cpp
// render.cpp ONLY does this:
void render_frame(Renderer* r, const VisualFrame& frame, ...);

// render.cpp NEVER does this:
// game.player.pos  // FORBIDDEN
```

### 5. Timeline Drives Everything
The `Timeline.gradient` array (0.0–1.0) is the single source of truth for rhythm intensity.

```cpp
float gradient = get_brightness_at_time(timeline, music_time);
float dt = real_dt * get_time_scale(gradient);  // Time dilation
int enemy_count = BASE_ENEMY_COUNT + gradient * ENEMY_COUNT_SCALE;  // Spawn count
float enemy_speed = base_speed * (1.0f + gradient);  // Movement speed
```

---

## Game Loop (main.cpp)

```cpp
while (running) {
    Systems::poll_input(events);                          // 1. Read input
    
    float gradient = get_brightness_at_time(timeline, time);
    float dt = real_dt * get_time_scale(gradient);
    
    Systems::update_combat(game, events, dt, timeline, time);  // 2. Combat
    Systems::update_movement(game, dt);                        // 3. Physics
    Systems::update_spawn(game, dt, gradient);                 // 4. Spawning
    Systems::update_score(game, events, dt);                   // 5. Score
    Systems::build_visual_frame(game, dt, visual_frame);       // 6. Animation
    Systems::update_camera(game.camera, game.player.pos, ...); // 7. Camera
    
    events.clear();                                       // 8. Flush events
    render_frame(renderer, visual_frame, timeline, time, running); // 9. Draw
}
```

**Critical:** Always call systems in this order. `build_visual_frame` must run after all game logic.

---

## Component Reference

### Player
- `pos`, `angle` — world position/rotation
- `state` — `Idle`, `Charging`, `Slashing`
- `state_timer` — counts down during attack
- `jump_start`, `jump_target`, `jump_control` — bezier curve points
- `ribbons` — **game logic owns this** (persisted between frames)
- `target_enemy` — index of enemy being attacked
- `has_slashed` — set true when slash phase executes hits
- `can_chain` — true when target enemy is dead during slash

### Enemy
- `pos`, `vel` — position and velocity
- `radius` — collision/render size
- `alive` — false after being slashed
- `flash_timer` — white flash duration after death
- `blow_away_timer`, `blow_away_vel`, `being_blown` — physics after death
- `base_speed` — movement speed multiplier

### ScoreData
- `points` — total score
- `level` — current level (0–10)
- `combo` — consecutive good hits
- `misses` — consecutive misses (affects penalty)
- `fill` — 0.0–1.0 progress to next level
- `display_*` — smoothly interpolated values for UI animation
- `feedback_timer` — hit/miss flash duration

---

## Adding New Features

### Add a New System
1. Declare function in `include/systems.hpp`
2. Implement in `src/systems.cpp`
3. Call in `main.cpp` game loop
4. Add to AGENTS.md system table

### Add a New Event Type
1. Add struct to `EventBus` in `include/events.hpp`
2. Add `std::vector<YourEvent>` member to `EventBus`
3. Clear vector in `events.cpp`
4. Emit in one system, consume in another

### Add a New Component Field
1. Add field to struct in `include/components.hpp`
2. Reset in `src/components.cpp`
3. Update any affected systems

### Add a New Enemy Type
**Do NOT subclass Enemy.** Add a type field:

```cpp
enum class EnemyType { Normal, Fast, Tank };
struct Enemy {
    EnemyType type = EnemyType::Normal;
    // ... existing fields
};
```

Then branch in `update_movement` or `spawn_enemy` based on type.

---

## Render System

### Architecture
- Always WASM/WebGL (no native/desktop path)
- OpenGLES 2.0 with single shader (position + color attributes)
- Batched vertex buffer: `GL_LINES` for outlines, `GL_TRIANGLES` for fills
- No SDL_Renderer — pure GL

### Frame Pipeline
1. Clear to black
2. Enable alpha blending
3. Draw ribbons (from VisualFrame)
4. Draw enemies (circles with inner ring)
5. Draw player triangle
6. Draw sword
7. Draw score bar
8. Draw progress bar with fisheye distortion
9. Swap buffers

### Adding New Visuals
Render **only** reads `VisualFrame`. If you need new visual data:

1. Add field to `VisualFrame` in `include/systems.hpp`
2. Set it in `build_visual_frame()` in `src/systems.cpp`
3. Read it in `render_frame()` in `src/render.cpp`

---

## Code Conventions

### Style
- C++11 standard
- Snake_case for locals/fields, camelCase for functions
- Global state prefixed with `g_` (minimize use)
- Structs only (no classes except `AudioDecoder`)
- Tab indentation, 4 spaces

### Module Rules

**systems.cpp (Game Logic):**
- Never include SDL.h
- Never access render internals
- Never interpolate visuals (raw positions only)
- Modify GameState, emit events

**render.cpp (Graphics):**
- Never change game rules or spawn logic
- Never read GameState directly (only VisualFrame)
- All drawing primitives stay here
- No game logic, no randomness

**main.cpp (Orchestration):**
- Only calls systems in order
- Handles game start/stop lifecycle
- No gameplay logic

---

## Build System (Devbox)

```bash
devbox run build   # Compile WASM
devbox run run     # Serve on localhost:6931
devbox run clean   # Remove build artifacts
```

Build flags: `-s USE_SDL=2 -s FULL_ES2=1 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1`

---

## Testing Checklist

When making changes, verify:
- [ ] `devbox run build` compiles WASM without errors
- [ ] `devbox run run` serves and opens browser
- [ ] Game starts on first click/keypress
- [ ] Player jumps to nearest enemy on attack
- [ ] Enemies spawn off-screen and chase player
- [ ] Sword slash kills target enemy
- [ ] Blow-away physics push nearby enemies back
- [ ] Ribbons appear during charge/slash and fade
- [ ] Score increases on hit, decreases on miss
- [ ] Score bar animates smoothly
- [ ] Progress bar responds to music
- [ ] Camera follows player with smooth zoom
- [ ] No console errors in browser dev tools

---

## Common Pitfalls

1. **Enemy reset() overwrites spawn data** — `Enemy::reset()` must ONLY reset state flags (alive, timers). Never reset position/velocity/radius (those are set by spawn logic).

2. **Ribbons disappear** — Must store in `player.ribbons` (persists), not create fresh each frame in `VisualFrame`.

3. **Systems out of order** — `build_visual_frame` must run AFTER all game logic updates. Never read GameState in render.

4. **Events not cleared** — Always call `events.clear()` at end of frame or events accumulate forever.

5. **Static state in systems** — Use static locals sparingly. Prefer storing state in `GameState` components.

6. **Hardcoded screen size** — Never hardcode 800x600. Use `get_screen_size()` from renderer or `camera.zoom` for spawn distance calculations.

---

## Dependencies

- minimp3: https://github.com/lieff/minimp3 (CC0)
- KissFFT: https://github.com/mborgerding/kissfft (BSD)
- Emscripten: https://emscripten.org/
- SDL2 (via Emscripten ports)
