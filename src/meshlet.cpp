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

#include "meshlet.hpp"
#include <array>
#include <cmath>
#include <unordered_map>

//! Computes the bounding sphere of a set of vertex positions.
static MeshletBounds computeBoundingSphere(const std::vector<MathLib::Vec3>& positions)
{
    if (positions.empty()) {
        return {};
    }

    // Start with the centroid as the initial centre.
    MathLib::Vec3 centre{0.0f, 0.0f, 0.0f};
    for (const MathLib::Vec3& p : positions) {
        centre = centre + p;
    }
    float inv_count{1.0f / static_cast<float>(positions.size())};
    centre = centre * inv_count;

    // Expand to encompass all points.
    float radius{0.0f};
    for (const MathLib::Vec3& p : positions) {
        float dist{(p - centre).length()};
        if (dist > radius) {
            radius = dist;
        }
    }

    return {centre, radius};
}

MeshData buildMeshlets(std::span<const MathLib::Vec3> positions, std::span<const uint32_t> indices)
{
    MeshData result{};
    uint32_t triangle_count{static_cast<uint32_t>(indices.size()) / 3};

    // Process triangles greedily — add to the current meshlet until it's full.
    Meshlet current_meshlet{};
    current_meshlet.vertex_offset = 0;
    current_meshlet.triangle_offset = 0;

    // Maps global vertex index → local meshlet vertex index for the current meshlet.
    std::unordered_map<uint32_t, uint8_t> vertex_map;
    std::vector<MathLib::Vec3> meshlet_positions; // For bounding sphere computation.

    for (uint32_t tri{0}; tri < triangle_count; ++tri) {
        uint32_t i0{indices[tri * 3 + 0]};
        uint32_t i1{indices[tri * 3 + 1]};
        uint32_t i2{indices[tri * 3 + 2]};

        // Count how many new vertices this triangle would add.
        uint32_t new_verts{0};
        if (vertex_map.find(i0) == vertex_map.end()) {
            ++new_verts;
        }
        if (vertex_map.find(i1) == vertex_map.end()) {
            ++new_verts;
        }
        if (vertex_map.find(i2) == vertex_map.end()) {
            ++new_verts;
        }

        // If this triangle would overflow the meshlet, finalise and start a new one.
        bool vertex_overflow{(current_meshlet.vertex_count + new_verts) > MAX_MESHLET_VERTICES};
        bool triangle_overflow{(current_meshlet.triangle_count + 1) > MAX_MESHLET_TRIANGLES};

        if (vertex_overflow || triangle_overflow) {
            // Finalise current meshlet.
            result.meshlets.push_back(current_meshlet);
            result.bounds.push_back(computeBoundingSphere(meshlet_positions));

            // Start a new meshlet.
            current_meshlet.vertex_offset = static_cast<uint32_t>(result.vertex_indices.size());
            current_meshlet.triangle_offset = static_cast<uint32_t>(result.triangle_indices.size());
            current_meshlet.vertex_count = 0;
            current_meshlet.triangle_count = 0;
            vertex_map.clear();
            meshlet_positions.clear();
        }

        // Map each vertex to a local meshlet index.
        std::array<uint32_t, 3> global_indices{i0, i1, i2};
        std::array<uint8_t, 3> local_indices{};

        for (uint32_t v{0}; v < 3; ++v) {
            uint32_t global_idx{global_indices[v]};
            std::unordered_map<uint32_t, uint8_t>::iterator it{vertex_map.find(global_idx)};
            if (it != vertex_map.end()) {
                local_indices[v] = it->second;
            } else {
                uint8_t local_idx{static_cast<uint8_t>(current_meshlet.vertex_count)};
                vertex_map[global_idx] = local_idx;
                local_indices[v] = local_idx;

                // Store the global vertex index in the vertex index buffer.
                result.vertex_indices.push_back(global_idx);
                meshlet_positions.push_back(positions[global_idx]);
                ++current_meshlet.vertex_count;
            }
        }

        // Store the triangle as 3 local vertex indices.
        result.triangle_indices.push_back(local_indices[0]);
        result.triangle_indices.push_back(local_indices[1]);
        result.triangle_indices.push_back(local_indices[2]);
        ++current_meshlet.triangle_count;
    }

    // Finalise the last meshlet.
    if (current_meshlet.triangle_count > 0) {
        result.meshlets.push_back(current_meshlet);
        result.bounds.push_back(computeBoundingSphere(meshlet_positions));
    }

    return result;
}

void generateUVSphere(uint32_t stacks, uint32_t slices, float radius, std::vector<MathLib::Vec3>& out_positions, std::vector<uint32_t>& out_indices)
{
    out_positions.clear();
    out_indices.clear();

    // Top pole.
    out_positions.push_back({0.0f, radius, 0.0f});

    // Middle rings.
    for (uint32_t i{1}; i < stacks; ++i) {
        float phi{MathLib::PI * static_cast<float>(i) / static_cast<float>(stacks)};
        float sin_phi{std::sin(phi)};
        float cos_phi{std::cos(phi)};

        for (uint32_t j{0}; j < slices; ++j) {
            float theta{2.0f * MathLib::PI * static_cast<float>(j) / static_cast<float>(slices)};
            float x{radius * sin_phi * std::cos(theta)};
            float y{radius * cos_phi};
            float z{radius * sin_phi * std::sin(theta)};
            out_positions.push_back({x, y, z});
        }
    }

    // Bottom pole.
    out_positions.push_back({0.0f, -radius, 0.0f});

    uint32_t top_index{0};
    uint32_t bottom_index{static_cast<uint32_t>(out_positions.size()) - 1};

    // Top cap triangles.
    for (uint32_t j{0}; j < slices; ++j) {
        uint32_t next{(j + 1) % slices};
        out_indices.push_back(top_index);
        out_indices.push_back(1 + j);
        out_indices.push_back(1 + next);
    }

    // Middle quads (two triangles each).
    for (uint32_t i{0}; i < stacks - 2; ++i) {
        uint32_t ring_start{1 + i * slices};
        uint32_t next_ring{1 + (i + 1) * slices};
        for (uint32_t j{0}; j < slices; ++j) {
            uint32_t next{(j + 1) % slices};
            out_indices.push_back(ring_start + j);
            out_indices.push_back(next_ring + j);
            out_indices.push_back(next_ring + next);

            out_indices.push_back(ring_start + j);
            out_indices.push_back(next_ring + next);
            out_indices.push_back(ring_start + next);
        }
    }

    // Bottom cap triangles.
    uint32_t last_ring{1 + (stacks - 2) * slices};
    for (uint32_t j{0}; j < slices; ++j) {
        uint32_t next{(j + 1) % slices};
        out_indices.push_back(bottom_index);
        out_indices.push_back(last_ring + next);
        out_indices.push_back(last_ring + j);
    }
}
