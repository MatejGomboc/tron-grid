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

/*!
    Generates a procedural heightmap terrain with the angular Tron aesthetic.

    The terrain is a grid of vertices displaced in Y by layered value noise,
    then quantised to discrete levels for the terraced/crystalline look.
    Normals are per-face (flat shading) to match the angular aesthetic.

    \param config Terrain generation parameters.
    \return TerrainMesh with vertices, indices, and positions.
*/
[[nodiscard]] TerrainMesh generateTerrain(const TerrainConfig& config);
