/*
    Copyright (C) 2026 Matej Gomboc https://github.com/MatejGomboc/tron-grid

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

#include "terrain.hpp"
#include <cmath>

//! Simple hash-based value noise (deterministic, no external dependencies).
static float valueNoise(float x, float z, uint32_t seed)
{
    // Hash the integer coordinates.
    int32_t ix{static_cast<int32_t>(std::floor(x))};
    int32_t iz{static_cast<int32_t>(std::floor(z))};

    // Fractional parts for interpolation.
    float fx{x - std::floor(x)};
    float fz{z - std::floor(z)};

    // Smoothstep for smooth interpolation.
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);

    // Hash function — produces pseudo-random values from integer coordinates.
    auto hash = [seed](int32_t px, int32_t pz) -> float {
        uint32_t h{static_cast<uint32_t>(px * 374761393 + pz * 668265263 + seed * 1274126177)};
        h = (h ^ (h >> 13)) * 1103515245;
        h = h ^ (h >> 16);
        return static_cast<float>(h & 0x7FFFFFFF) / static_cast<float>(0x7FFFFFFF);
    };

    // Bilinear interpolation of the 4 corner values.
    float v00{hash(ix, iz)};
    float v10{hash(ix + 1, iz)};
    float v01{hash(ix, iz + 1)};
    float v11{hash(ix + 1, iz + 1)};

    float v0{v00 + fx * (v10 - v00)};
    float v1{v01 + fx * (v11 - v01)};
    return v0 + fz * (v1 - v0);
}

//! Layered noise (octaves) for terrain detail.
static float layeredNoise(float x, float z, uint32_t octaves, float frequency, uint32_t seed)
{
    float value{0.0f};
    float amplitude{1.0f};
    float total_amplitude{0.0f};
    float freq{frequency};

    for (uint32_t i{0}; i < octaves; ++i) {
        value += amplitude * valueNoise(x * freq, z * freq, seed + i);
        total_amplitude += amplitude;
        amplitude *= 0.5f;
        freq *= 2.0f;
    }

    return value / total_amplitude;
}

TerrainMesh generateTerrain(const TerrainConfig& config)
{
    TerrainMesh result{};

    uint32_t verts_per_side{config.grid_size + 1};

    // Generate heightmap.
    std::vector<float> heightmap(static_cast<size_t>(verts_per_side) * verts_per_side);
    float half_size{static_cast<float>(config.grid_size) * config.tile_spacing * 0.5f};

    for (uint32_t z{0}; z < verts_per_side; ++z) {
        for (uint32_t x{0}; x < verts_per_side; ++x) {
            float wx{static_cast<float>(x) * config.tile_spacing - half_size};
            float wz{static_cast<float>(z) * config.tile_spacing - half_size};

            float h{layeredNoise(wx, wz, config.noise_octaves, config.noise_frequency, config.seed)};

            // Quantise for the angular/terraced look.
            if (config.quantise_levels > 0) {
                float levels{static_cast<float>(config.quantise_levels)};
                h = std::floor(h * levels) / levels;
            }

            h *= config.height_scale;
            heightmap[z * verts_per_side + x] = h;
        }
    }

    // Generate per-face vertices (flat shading — 3 unique vertices per triangle).
    // Each grid cell produces 2 triangles = 6 vertices.
    uint32_t total_triangles{config.grid_size * config.grid_size * 2};
    result.vertices.reserve(total_triangles * 3);
    result.positions.reserve(total_triangles * 3);
    result.indices.reserve(total_triangles * 3);

    uint32_t vertex_index{0};

    for (uint32_t z{0}; z < config.grid_size; ++z) {
        for (uint32_t x{0}; x < config.grid_size; ++x) {
            // 4 corner positions of this grid cell.
            float x0{static_cast<float>(x) * config.tile_spacing - half_size};
            float x1{static_cast<float>(x + 1) * config.tile_spacing - half_size};
            float z0{static_cast<float>(z) * config.tile_spacing - half_size};
            float z1{static_cast<float>(z + 1) * config.tile_spacing - half_size};

            float h00{heightmap[z * verts_per_side + x]};
            float h10{heightmap[z * verts_per_side + (x + 1)]};
            float h01{heightmap[(z + 1) * verts_per_side + x]};
            float h11{heightmap[(z + 1) * verts_per_side + (x + 1)]};

            MathLib::Vec3 p00{x0, h00, z0};
            MathLib::Vec3 p10{x1, h10, z0};
            MathLib::Vec3 p01{x0, h01, z1};
            MathLib::Vec3 p11{x1, h11, z1};

            // Triangle 1: p00, p10, p01.
            MathLib::Vec3 n1{(p01 - p00).cross(p10 - p00).normalised()};
            result.positions.push_back(p00);
            result.positions.push_back(p10);
            result.positions.push_back(p01);
            result.vertices.push_back({{p00.x, p00.y, p00.z}, {n1.x, n1.y, n1.z}, {0.0f, 0.0f}});
            result.vertices.push_back({{p10.x, p10.y, p10.z}, {n1.x, n1.y, n1.z}, {0.0f, 0.0f}});
            result.vertices.push_back({{p01.x, p01.y, p01.z}, {n1.x, n1.y, n1.z}, {0.0f, 0.0f}});
            result.indices.push_back(vertex_index++);
            result.indices.push_back(vertex_index++);
            result.indices.push_back(vertex_index++);

            // Triangle 2: p10, p11, p01.
            MathLib::Vec3 n2{(p01 - p10).cross(p11 - p10).normalised()};
            result.positions.push_back(p10);
            result.positions.push_back(p11);
            result.positions.push_back(p01);
            result.vertices.push_back({{p10.x, p10.y, p10.z}, {n2.x, n2.y, n2.z}, {0.0f, 0.0f}});
            result.vertices.push_back({{p11.x, p11.y, p11.z}, {n2.x, n2.y, n2.z}, {0.0f, 0.0f}});
            result.vertices.push_back({{p01.x, p01.y, p01.z}, {n2.x, n2.y, n2.z}, {0.0f, 0.0f}});
            result.indices.push_back(vertex_index++);
            result.indices.push_back(vertex_index++);
            result.indices.push_back(vertex_index++);
        }
    }

    // Compute bounding radius from origin.
    float max_dist{0.0f};
    for (const MathLib::Vec3& p : result.positions) {
        float dist{p.length()};
        if (dist > max_dist) {
            max_dist = dist;
        }
    }
    result.bounding_radius = max_dist;

    return result;
}
