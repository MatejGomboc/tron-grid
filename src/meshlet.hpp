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

#pragma once

#include <math/vector.hpp>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

//! Maximum vertices per meshlet (NVIDIA optimal).
constexpr uint32_t MAX_MESHLET_VERTICES{64};

//! Maximum triangles per meshlet. Limited to 84 to support barycentric vertex
//! duplication in the mesh shader (84 × 3 = 252 vertices ≤ 255 output limit).
constexpr uint32_t MAX_MESHLET_TRIANGLES{84};

//! Describes one meshlet — a small fixed-size chunk of mesh geometry.
struct Meshlet {
    uint32_t vertex_offset{0}; //!< Offset into the meshlet vertex index buffer.
    uint32_t vertex_count{0}; //!< Number of unique vertices (max 64).
    uint32_t triangle_offset{0}; //!< Offset into the meshlet triangle index buffer.
    uint32_t triangle_count{0}; //!< Number of triangles (max 124).
};

//! Bounding sphere for per-meshlet frustum culling (local space).
struct MeshletBounds {
    MathLib::Vec3 centre{0.0f, 0.0f, 0.0f}; //!< Local-space bounding sphere centre.
    float radius{0.0f}; //!< Bounding sphere radius.
};

/*!
    Complete meshlet representation of a mesh.

    Contains the meshlet descriptors, bounds, and the vertex/triangle index
    buffers that mesh shaders read. Generated from raw vertex + index data
    via buildMeshlets().
*/
struct MeshData {
    std::vector<Meshlet> meshlets; //!< Meshlet descriptors.
    std::vector<MeshletBounds> bounds; //!< Per-meshlet bounding spheres.
    std::vector<uint32_t> vertex_indices; //!< Global vertex indices referenced by each meshlet.
    std::vector<uint8_t> triangle_indices; //!< Packed triangle indices (3 uint8_t per triangle, into vertex_indices).
};

//! One LOD level of a mesh — offset and count into the global meshlet array.
struct MeshLOD {
    uint32_t meshlet_offset{0}; //!< First meshlet index for this LOD level.
    uint32_t meshlet_count{0}; //!< Number of meshlets in this LOD level.
};

//! Maximum LOD levels per mesh.
constexpr uint32_t MAX_LOD_LEVELS{4};

//! Describes a mesh with up to MAX_LOD_LEVELS LOD levels.
struct MeshDescriptor {
    uint32_t lod_count{0}; //!< Number of LOD levels (1 = no LOD).
    std::array<MeshLOD, MAX_LOD_LEVELS> lods{}; //!< LOD levels (0 = highest detail).
    float bounding_radius{0.0f}; //!< Local-space bounding sphere radius (for culling).
};

/*!
    Generates a UV sphere mesh (positions + indices).

    \param stacks Number of horizontal slices (latitude).
    \param slices Number of vertical segments (longitude).
    \param radius Sphere radius.
    \param out_positions Output vertex positions.
    \param out_indices Output triangle indices.
*/
void generateUVSphere(uint32_t stacks, uint32_t slices, float radius, std::vector<MathLib::Vec3>& out_positions, std::vector<uint32_t>& out_indices);

/*!
    Builds meshlets from mesh geometry.

    Partitions triangles into meshlets of up to MAX_MESHLET_VERTICES vertices
    and MAX_MESHLET_TRIANGLES triangles. Each meshlet has its own local vertex
    index buffer and triangle index buffer (uint8_t triplets referencing the
    local vertex indices).

    \param positions Vertex positions (one per vertex).
    \param indices Triangle indices (3 per triangle).
    \return MeshData containing all meshlet buffers.
*/
[[nodiscard]] MeshData buildMeshlets(std::span<const MathLib::Vec3> positions, std::span<const uint32_t> indices);
