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

#include "pipeline.hpp"
#include <math/vector.hpp>
#include <cstdint>
#include <vector>

//! Configuration for procedural terrain generation.
struct TerrainConfig {
    uint32_t grid_size{64}; //!< Number of cells along each axis (grid_size × grid_size).
    float tile_spacing{1.0f}; //!< World-space distance between grid vertices.
    float height_scale{5.0f}; //!< Maximum height displacement.
    float noise_frequency{0.08f}; //!< Noise frequency (lower = smoother terrain).
    uint32_t noise_octaves{4}; //!< Number of noise layers for detail.
    uint32_t quantise_levels{8}; //!< Number of discrete height levels (0 = no quantisation).
    uint32_t seed{42}; //!< Random seed for noise generation.
};

//! Result of terrain generation — ready for meshlet building and GPU upload.
struct TerrainMesh {
    std::vector<Vertex> vertices; //!< Per-face vertices (3 per triangle, flat shaded).
    std::vector<uint32_t> indices; //!< Triangle indices.
    std::vector<MathLib::Vec3> positions; //!< Vertex positions (for meshlet generation).
    float bounding_radius{0.0f}; //!< Bounding sphere radius from origin.
};

//! Sub-mesh for a single colour of neon tube geometry (cyan or orange).
struct NeonSubMesh {
    std::vector<Vertex> vertices; //!< Per-face vertices (6 per edge quad, flat shaded).
    std::vector<uint32_t> indices; //!< Triangle indices.
    std::vector<MathLib::Vec3> positions; //!< Vertex positions (for meshlet generation).
};

//! Result of neon tube generation — thin emissive quads along terrain grid edges.
struct NeonTubeMesh {
    NeonSubMesh cyan; //!< Primary grid tubes (cyan emissive).
    NeonSubMesh orange; //!< Major grid line tubes (orange emissive).
    float bounding_radius{0.0f}; //!< Bounding sphere radius from origin.
};

/*!
    Generates a procedural heightmap terrain with the angular Tron aesthetic.

    The terrain is a grid of vertices displaced in Y by layered value noise,
    then quantised to discrete levels for the terraced/crystalline look.
    Normals are per-face (flat shading) to match the angular aesthetic.

    \param config Terrain generation parameters.
    \return TerrainMesh with vertices, indices, and positions.
*/
[[nodiscard]] TerrainMesh generateTerrain(const TerrainConfig& config);

/*!
    Generates thin emissive quad geometry along every horizontal and vertical
    terrain grid edge. Each quad is slightly offset above the terrain surface
    so that ray tracing can sample neon tubes as area light sources.

    Edges are classified as cyan (primary grid) or orange (major grid lines,
    every 8th row/column) and separated into two sub-meshes for distinct
    material assignment.

    \param config Terrain generation parameters (same config as generateTerrain).
    \return NeonTubeMesh with cyan and orange sub-meshes.
*/
[[nodiscard]] NeonTubeMesh generateNeonTubes(const TerrainConfig& config);

/*!
    Generates a flat-shaded axis-aligned box mesh — 12 triangles, 36 vertices
    (per-face vertices, no indexed sharing so face normals are correct).

    The output structure layout matches NeonSubMesh so the box can flow through
    the same meshlet/BLAS/vertex-buffer plumbing used by the neon tube and
    sphere meshes.

    \param centre World-space centre of the box.
    \param half_extents Half-size along each axis (so the box spans
        centre - half_extents to centre + half_extents).
    \return NeonSubMesh with vertices, indices, and positions populated.
*/
[[nodiscard]] NeonSubMesh generateBox(const MathLib::Vec3& centre, const MathLib::Vec3& half_extents);
