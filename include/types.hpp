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

struct Vec3 {
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    float len() const { return sqrtf(x*x + y*y + z*z); }
    Vec3 normalized() const { float l = len(); return l > 0.001f ? Vec3(x/l, y/l, z/l) : Vec3(0,0,0); }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

struct Mat4 {
    float m[16];
    static Mat4 identity() {
        Mat4 r;
        for (int i = 0; i < 16; i++) r.m[i] = 0.0f;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
    static Mat4 translate(float x, float y, float z) {
        Mat4 r = identity();
        r.m[12] = x; r.m[13] = y; r.m[14] = z;
        return r;
    }
    static Mat4 rotate_x(float angle) {
        Mat4 r = identity();
        float c = cosf(angle), s = sinf(angle);
        r.m[5] = c; r.m[6] = -s;
        r.m[9] = s; r.m[10] = c;
        return r;
    }
    static Mat4 rotate_y(float angle) {
        Mat4 r = identity();
        float c = cosf(angle), s = sinf(angle);
        r.m[0] = c; r.m[2] = s;
        r.m[8] = -s; r.m[10] = c;
        return r;
    }
    static Mat4 rotate_z(float angle) {
        Mat4 r = identity();
        float c = cosf(angle), s = sinf(angle);
        r.m[0] = c; r.m[1] = -s;
        r.m[4] = s; r.m[5] = c;
        return r;
    }
    static Mat4 scale(float sx, float sy, float sz) {
        Mat4 r = identity();
        r.m[0] = sx; r.m[5] = sy; r.m[10] = sz;
        return r;
    }
};

inline Vec3 transform_point(const Mat4& mat, const Vec3& p) {
    float x = mat.m[0]*p.x + mat.m[4]*p.y + mat.m[8]*p.z + mat.m[12];
    float y = mat.m[1]*p.x + mat.m[5]*p.y + mat.m[9]*p.z + mat.m[13];
    float z = mat.m[2]*p.x + mat.m[6]*p.y + mat.m[10]*p.z + mat.m[14];
    return Vec3(x, y, z);
}

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r.m[i + j*4] = 0.0f;
            for (int k = 0; k < 4; k++) {
                r.m[i + j*4] += a.m[i + k*4] * b.m[k + j*4];
            }
        }
    }
    return r;
}

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
