#pragma once

#include <cmath>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float x, float y) : x(x), y(y) {}
    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
    float len() const { return sqrtf(x*x + y*y); }
    Vec2 normalized() const { float l = len(); return l > 0.001f ? Vec2(x/l, y/l) : Vec2(0,0); }
};

inline Vec2 operator*(float s, const Vec2& v) { return v * s; }

inline float angle_diff(float a, float b) {
    float diff = a - b;
    while (diff > M_PI) diff -= 2.0f * M_PI;
    while (diff < -M_PI) diff += 2.0f * M_PI;
    return diff;
}

inline float randf() { return (float)rand() / (float)RAND_MAX; }
inline float randf(float a, float b) { return a + randf() * (b - a); }

inline float ease_out_cubic(float t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    float u = 1 - t;
    return 1 - u*u*u;
}

inline float ease_in_cubic(float t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    return t*t*t;
}

inline float ease_out_quad(float t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    float u = 1 - t;
    return 1 - u*u;
}

inline Vec2 bezier(float t, Vec2 p0, Vec2 p1, Vec2 p2) {
    float u = 1 - t;
    return p0*(u*u) + p1*(2*u*t) + p2*(t*t);
}
