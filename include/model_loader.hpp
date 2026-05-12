#pragma once
#include "types.hpp"
#include <vector>

struct MeshModel {
    std::vector<Vec3> vertices;
    Vec3 bounds_min;
    Vec3 bounds_max;
    std::vector<Vec2> collision_poly;
    Vec3 tip_vertex;
    float default_scale = 1.0f;

    bool load_gltf(const char* path);
};
