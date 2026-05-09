#pragma once

#include "game.hpp"
#include "audio.hpp"

// Opaque render state - implementation hidden in render.cpp
struct Renderer;

// --- Lifecycle ---
Renderer* createRenderer(const char* title, int width, int height);
void destroyRenderer(Renderer* r);
bool isRendererValid(Renderer* r);

// --- Animation (render team owns this) ---
void updateAnimations(GameState& game, float dt);

// --- Frame ---
void getScreenSize(Renderer* r, int& w, int& h);
void renderFrame(Renderer* r, const GameState& game, const Timeline& timeline,
                 float time, bool running);

// --- Input ---
// Returns true if should quit
bool pollEvents(GameState& game, const Timeline& timeline, bool& attack, bool& start);
