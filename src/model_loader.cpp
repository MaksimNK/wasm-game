#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "model_loader.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

static void update_bounds(Vec3& min_v, Vec3& max_v, const Vec3& v) {
    if (v.x < min_v.x) min_v.x = v.x;
    if (v.y < min_v.y) min_v.y = v.y;
    if (v.z < min_v.z) min_v.z = v.z;
    if (v.x > max_v.x) max_v.x = v.x;
    if (v.y > max_v.y) max_v.y = v.y;
    if (v.z > max_v.z) max_v.z = v.z;
}

static Mat4 node_local_matrix(cgltf_node* node) {
    Mat4 local = Mat4::identity();
    if (node->has_matrix) {
        for (int i = 0; i < 16; i++) local.m[i] = (float)node->matrix[i];
    } else {
        Mat4 T = Mat4::identity();
        if (node->has_translation) {
            T = Mat4::translate((float)node->translation[0], (float)node->translation[1], (float)node->translation[2]);
        }
        Mat4 R = Mat4::identity();
        if (node->has_rotation) {
            float x = (float)node->rotation[0];
            float y = (float)node->rotation[1];
            float z = (float)node->rotation[2];
            float w = (float)node->rotation[3];
            float xx = x*x, yy = y*y, zz = z*z;
            float xy = x*y, xz = x*z, yz = y*z;
            float wx = w*x, wy = w*y, wz = w*z;
            R.m[0] = 1 - 2*(yy+zz); R.m[1] = 2*(xy+wz);     R.m[2] = 2*(xz-wy);
            R.m[4] = 2*(xy-wz);     R.m[5] = 1 - 2*(xx+zz); R.m[6] = 2*(yz+wx);
            R.m[8] = 2*(xz+wy);     R.m[9] = 2*(yz-wx);     R.m[10] = 1 - 2*(xx+yy);
        }
        Mat4 S = Mat4::identity();
        if (node->has_scale) {
            S = Mat4::scale((float)node->scale[0], (float)node->scale[1], (float)node->scale[2]);
        }
        local = multiply(T, multiply(R, S));
    }
    return local;
}

static void traverse_node(cgltf_node* node, const Mat4& parent, std::vector<Vec3>& verts,
                          Vec3& bmin, Vec3& bmax) {
    Mat4 local = node_local_matrix(node);
    Mat4 world = multiply(parent, local);

    if (node->mesh) {
        for (size_t pi = 0; pi < node->mesh->primitives_count; pi++) {
            cgltf_primitive* prim = &node->mesh->primitives[pi];
            if (prim->type != cgltf_primitive_type_triangles) continue;

            cgltf_accessor* pos_acc = nullptr;
            for (size_t ai = 0; ai < prim->attributes_count; ai++) {
                if (prim->attributes[ai].type == cgltf_attribute_type_position) {
                    pos_acc = prim->attributes[ai].data;
                    break;
                }
            }
            if (!pos_acc) continue;

            if (prim->indices) {
                cgltf_accessor* idx_acc = prim->indices;
                for (size_t i = 0; i + 2 < idx_acc->count; i += 3) {
                    for (int vi = 0; vi < 3; vi++) {
                        cgltf_size idx = cgltf_accessor_read_index(idx_acc, i + vi);
                        float pos[3];
                        cgltf_accessor_read_float(pos_acc, idx, pos, 3);
                        Vec3 v((float)pos[0], (float)pos[1], (float)pos[2]);
                        Vec3 wv = transform_point(world, v);
                        verts.push_back(wv);
                        update_bounds(bmin, bmax, wv);
                    }
                }
            } else {
                for (size_t i = 0; i + 2 < pos_acc->count; i += 3) {
                    for (int vi = 0; vi < 3; vi++) {
                        float pos[3];
                        cgltf_accessor_read_float(pos_acc, i + vi, pos, 3);
                        Vec3 v((float)pos[0], (float)pos[1], (float)pos[2]);
                        Vec3 wv = transform_point(world, v);
                        verts.push_back(wv);
                        update_bounds(bmin, bmax, wv);
                    }
                }
            }
        }
    }

    for (size_t ci = 0; ci < node->children_count; ci++) {
        traverse_node(node->children[ci], world, verts, bmin, bmax);
    }
}

bool MeshModel::load_gltf(const char* path) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path, &data);
    if (result != cgltf_result_success) {
        fprintf(stderr, "[MODEL] Failed to parse %s\n", path);
        return false;
    }

    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) {
        fprintf(stderr, "[MODEL] Failed to load buffers for %s\n", path);
        cgltf_free(data);
        return false;
    }

    vertices.clear();
    bounds_min = Vec3(1e10f, 1e10f, 1e10f);
    bounds_max = Vec3(-1e10f, -1e10f, -1e10f);

    for (size_t si = 0; si < data->scenes_count; si++) {
        cgltf_scene* scene = &data->scenes[si];
        for (size_t ni = 0; ni < scene->nodes_count; ni++) {
            traverse_node(scene->nodes[ni], Mat4::identity(), vertices, bounds_min, bounds_max);
        }
    }

    cgltf_free(data);

    if (vertices.empty()) {
        fprintf(stderr, "[MODEL] No vertices loaded from %s\n", path);
        return false;
    }

    // Determine dominant axis and apply base rotation to make model Y-up
    float ex = bounds_max.x - bounds_min.x;
    float ey = bounds_max.y - bounds_min.y;
    float ez = bounds_max.z - bounds_min.z;

    Mat4 base_rot = Mat4::identity();
    bool z_dominant = (ez >= ex && ez >= ey);
    bool x_dominant = (ex >= ey && ex >= ez);

    if (z_dominant) {
        // Model is Z-oriented; rotate +90° around X to make Z point to Y
        base_rot = Mat4::rotate_x(M_PI * 0.5f);
    } else if (x_dominant) {
        // Model is X-oriented; rotate -90° around Z to make X point to Y
        base_rot = Mat4::rotate_z(-M_PI * 0.5f);
    }
    // If Y-dominant, no rotation needed

    if (base_rot.m[5] != 1.0f || base_rot.m[10] != 1.0f) {
        for (auto& v : vertices) {
            v = transform_point(base_rot, v);
        }
        // Recompute bounds
        bounds_min = Vec3(1e10f, 1e10f, 1e10f);
        bounds_max = Vec3(-1e10f, -1e10f, -1e10f);
        for (const auto& v : vertices) {
            update_bounds(bounds_min, bounds_max, v);
        }
    }

    collision_poly.clear();
    collision_poly.push_back(Vec2(bounds_min.x, bounds_min.y));
    collision_poly.push_back(Vec2(bounds_max.x, bounds_min.y));
    collision_poly.push_back(Vec2(bounds_max.x, bounds_max.y));
    collision_poly.push_back(Vec2(bounds_min.x, bounds_max.y));

    // Center model in X/Z, align bottom (handle) to origin in Y
    Vec3 offset(-(bounds_min.x + bounds_max.x) * 0.5f, -bounds_min.y, -(bounds_min.z + bounds_max.z) * 0.5f);
    for (auto& v : vertices) {
        v = v + offset;
    }
    bounds_min = bounds_min + offset;
    bounds_max = bounds_max + offset;
    for (auto& p : collision_poly) {
        p.x += offset.x;
        p.y += offset.y;
    }

    // Tip is at max Y (blade points in +Y after reorientation)
    tip_vertex = vertices[0];
    for (const auto& v : vertices) {
        if (v.y > tip_vertex.y) tip_vertex = v;
    }

    float model_height = bounds_max.y - bounds_min.y;
    if (model_height > 0.001f) {
        default_scale = 1.0f / model_height;
    }

    printf("[MODEL] Loaded %zu triangles from %s, bounds [%.3f,%.3f,%.3f] to [%.3f,%.3f,%.3f], scale=%.4f\n",
           vertices.size() / 3, path,
           bounds_min.x, bounds_min.y, bounds_min.z,
           bounds_max.x, bounds_max.y, bounds_max.z,
           default_scale);

    return true;
}
