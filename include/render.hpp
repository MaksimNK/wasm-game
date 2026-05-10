#pragma once

#include "systems.hpp"

struct Renderer;

Renderer* create_renderer(const char* title);
void destroy_renderer(Renderer* r);
bool is_renderer_valid(Renderer* r);
void get_screen_size(Renderer* r, int& w, int& h);
void render_frame(Renderer* r, const VisualFrame& frame, const Timeline& timeline, float time, bool running);
