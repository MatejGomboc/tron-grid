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
[[nodiscard]] static float valueNoise(float x, float z, uint32_t seed)
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
        // Use unsigned arithmetic to avoid signed integer overflow UB.
        uint32_t h{static_cast<uint32_t>(px) * 374761393u + static_cast<uint32_t>(pz) * 668265263u + seed * 1274126177u};
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
[[nodiscard]] static float layeredNoise(float x, float z, uint32_t octaves, float frequency, uint32_t seed)
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

//! Neon tube half-width in metres (full width = 0.02 m).
constexpr float NEON_TUBE_HALF_WIDTH{0.01f};

//! Normal offset to lift neon tubes above the terrain surface.
constexpr float NEON_TUBE_SURFACE_OFFSET{0.005f};

//! Major grid line interval (every Nth grid cell gets orange neon tubes).
constexpr float NEON_MAJOR_GRID_SPACING{8.0f};

//! Epsilon for near-zero cross product check (edge nearly parallel to up vector).
constexpr float CROSS_PRODUCT_EPSILON{0.000001f};

//! Emits a thin quad along the edge from A to B, appending to the target sub-mesh.
static void emitEdgeQuad(NeonSubMesh& mesh, const MathLib::Vec3& a, const MathLib::Vec3& b)
{
    MathLib::Vec3 edge{b - a};
    MathLib::Vec3 up{0.0f, 1.0f, 0.0f};
    MathLib::Vec3 side{edge.cross(up)};

    // If edge is nearly vertical, use alternative reference direction.
    if (side.lengthSquared() < CROSS_PRODUCT_EPSILON) {
        up = MathLib::Vec3{1.0f, 0.0f, 0.0f};
        side = edge.cross(up);
    }
    side = side.normalised();

    MathLib::Vec3 offset{0.0f, NEON_TUBE_SURFACE_OFFSET, 0.0f};
    MathLib::Vec3 hw{side * NEON_TUBE_HALF_WIDTH};

    // Quad corners: p0-p1 at endpoint A, p2-p3 at endpoint B.
    MathLib::Vec3 p0{a + offset - hw};
    MathLib::Vec3 p1{a + offset + hw};
    MathLib::Vec3 p2{b + offset + hw};
    MathLib::Vec3 p3{b + offset - hw};

    // Face normal (upward for horizontal-ish quads).
    MathLib::Vec3 face_normal{(p1 - p0).cross(p2 - p0).normalised()};

    uint32_t base_index{static_cast<uint32_t>(mesh.vertices.size())};

    // Triangle 1: p0, p1, p2. Smooth normal = face normal for flat quads.
    mesh.positions.push_back(p0);
    mesh.positions.push_back(p1);
    mesh.positions.push_back(p2);
    mesh.vertices.push_back({{p0.x, p0.y, p0.z}, {face_normal.x, face_normal.y, face_normal.z}, {0.0f, 0.0f}, {face_normal.x, face_normal.y, face_normal.z}, 0.0f});
    mesh.vertices.push_back({{p1.x, p1.y, p1.z}, {face_normal.x, face_normal.y, face_normal.z}, {0.0f, 0.0f}, {face_normal.x, face_normal.y, face_normal.z}, 0.0f});
    mesh.vertices.push_back({{p2.x, p2.y, p2.z}, {face_normal.x, face_normal.y, face_normal.z}, {0.0f, 0.0f}, {face_normal.x, face_normal.y, face_normal.z}, 0.0f});
    mesh.indices.push_back(base_index);
    mesh.indices.push_back(base_index + 1);
    mesh.indices.push_back(base_index + 2);

    // Triangle 2: p0, p2, p3.
    mesh.positions.push_back(p0);
    mesh.positions.push_back(p2);
    mesh.positions.push_back(p3);
    mesh.vertices.push_back({{p0.x, p0.y, p0.z}, {face_normal.x, face_normal.y, face_normal.z}, {0.0f, 0.0f}, {face_normal.x, face_normal.y, face_normal.z}, 0.0f});
    mesh.vertices.push_back({{p2.x, p2.y, p2.z}, {face_normal.x, face_normal.y, face_normal.z}, {0.0f, 0.0f}, {face_normal.x, face_normal.y, face_normal.z}, 0.0f});
    mesh.vertices.push_back({{p3.x, p3.y, p3.z}, {face_normal.x, face_normal.y, face_normal.z}, {0.0f, 0.0f}, {face_normal.x, face_normal.y, face_normal.z}, 0.0f});
    mesh.indices.push_back(base_index + 3);
    mesh.indices.push_back(base_index + 4);
    mesh.indices.push_back(base_index + 5);
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

    // Compute per-vertex smooth normals from the RAW (un-quantised) heightmap gradient.
    // The quantised heightmap has sharp step functions whose central differences produce
    // large tilted normals at step boundaries. The raw noise surface is smooth — its
    // gradient gives a continuous normal field that follows the "underlying terrain shape,"
    // producing correct stretched reflections across terrace steps (like a car hood dent).
    std::vector<MathLib::Vec3> smooth_normals(static_cast<size_t>(verts_per_side) * verts_per_side);
    for (uint32_t z{0}; z < verts_per_side; ++z) {
        for (uint32_t x{0}; x < verts_per_side; ++x) {
            float wx{static_cast<float>(x) * config.tile_spacing - half_size};
            float wz{static_cast<float>(z) * config.tile_spacing - half_size};

            // Sample the raw noise (before quantisation) at offset positions for finite differences.
            float eps{config.tile_spacing * 0.5f};
            float h_xp{layeredNoise(wx + eps, wz, config.noise_octaves, config.noise_frequency, config.seed) * config.height_scale};
            float h_xn{layeredNoise(wx - eps, wz, config.noise_octaves, config.noise_frequency, config.seed) * config.height_scale};
            float h_zp{layeredNoise(wx, wz + eps, config.noise_octaves, config.noise_frequency, config.seed) * config.height_scale};
            float h_zn{layeredNoise(wx, wz - eps, config.noise_octaves, config.noise_frequency, config.seed) * config.height_scale};

            float dhdx{(h_xp - h_xn) / (2.0f * eps)};
            float dhdz{(h_zp - h_zn) / (2.0f * eps)};

            smooth_normals[z * verts_per_side + x] = MathLib::Vec3{-dhdx, 1.0f, -dhdz}.normalised();
        }
    }

    // Generate per-face vertices (flat shading — 3 unique vertices per triangle).
    // Each grid cell produces 2 triangles = 6 vertices.
    // Each vertex stores both the flat face normal (for shading) and the smooth
    // heightmap-gradient normal (for continuous reflections across tile boundaries).
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

            // Smooth normals at each corner.
            MathLib::Vec3 sn00{smooth_normals[z * verts_per_side + x]};
            MathLib::Vec3 sn10{smooth_normals[z * verts_per_side + (x + 1)]};
            MathLib::Vec3 sn01{smooth_normals[(z + 1) * verts_per_side + x]};
            MathLib::Vec3 sn11{smooth_normals[(z + 1) * verts_per_side + (x + 1)]};

            // Triangle 1: p00, p10, p01.
            // p00 is opposite the diagonal (p10-p01) — flag with uv.x = 1.0.
            MathLib::Vec3 n1{(p01 - p00).cross(p10 - p00).normalised()};
            result.positions.push_back(p00);
            result.positions.push_back(p10);
            result.positions.push_back(p01);
            result.vertices.push_back({{p00.x, p00.y, p00.z}, {n1.x, n1.y, n1.z}, {1.0f, 0.0f}, {sn00.x, sn00.y, sn00.z}, 0.0f});
            result.vertices.push_back({{p10.x, p10.y, p10.z}, {n1.x, n1.y, n1.z}, {0.0f, 0.0f}, {sn10.x, sn10.y, sn10.z}, 0.0f});
            result.vertices.push_back({{p01.x, p01.y, p01.z}, {n1.x, n1.y, n1.z}, {0.0f, 0.0f}, {sn01.x, sn01.y, sn01.z}, 0.0f});
            result.indices.push_back(vertex_index++);
            result.indices.push_back(vertex_index++);
            result.indices.push_back(vertex_index++);

            // Triangle 2: p10, p11, p01.
            // p11 is opposite the diagonal (p10-p01) — flag with uv.x = 1.0.
            MathLib::Vec3 n2{(p01 - p10).cross(p11 - p10).normalised()};
            result.positions.push_back(p10);
            result.positions.push_back(p11);
            result.positions.push_back(p01);
            result.vertices.push_back({{p10.x, p10.y, p10.z}, {n2.x, n2.y, n2.z}, {0.0f, 0.0f}, {sn10.x, sn10.y, sn10.z}, 0.0f});
            result.vertices.push_back({{p11.x, p11.y, p11.z}, {n2.x, n2.y, n2.z}, {1.0f, 0.0f}, {sn11.x, sn11.y, sn11.z}, 0.0f});
            result.vertices.push_back({{p01.x, p01.y, p01.z}, {n2.x, n2.y, n2.z}, {0.0f, 0.0f}, {sn01.x, sn01.y, sn01.z}, 0.0f});
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

NeonTubeMesh generateNeonTubes(const TerrainConfig& config)
{
    NeonTubeMesh result{};

    uint32_t verts_per_side{config.grid_size + 1};

    // Generate heightmap (same algorithm and seed as generateTerrain).
    std::vector<float> heightmap(static_cast<size_t>(verts_per_side) * verts_per_side);
    float half_size{static_cast<float>(config.grid_size) * config.tile_spacing * 0.5f};

    for (uint32_t z{0}; z < verts_per_side; ++z) {
        for (uint32_t x{0}; x < verts_per_side; ++x) {
            float wx{static_cast<float>(x) * config.tile_spacing - half_size};
            float wz{static_cast<float>(z) * config.tile_spacing - half_size};

            float h{layeredNoise(wx, wz, config.noise_octaves, config.noise_frequency, config.seed)};

            if (config.quantise_levels > 0) {
                float levels{static_cast<float>(config.quantise_levels)};
                h = std::floor(h * levels) / levels;
            }

            h *= config.height_scale;
            heightmap[z * verts_per_side + x] = h;
        }
    }

    // Helper: world-space position of heightmap vertex (gx, gz).
    auto vertexPos = [&](uint32_t gx, uint32_t gz) -> MathLib::Vec3 {
        float wx{static_cast<float>(gx) * config.tile_spacing - half_size};
        float wz{static_cast<float>(gz) * config.tile_spacing - half_size};
        float wy{heightmap[gz * verts_per_side + gx]};
        return {wx, wy, wz};
    };

    // Helper: check if a world-space coordinate lies on a major grid line.
    // Uses floor(abs(x)) (not abs(floor(x))) so classification is symmetric around
    // the origin — matches the shader's neonEmissiveColour() without this fix the
    // bands shift by one cell on negative coordinates.
    auto isOnMajorGridLine = [](float world_coord) -> bool {
        float mod_val{std::fmod(std::floor(std::abs(world_coord)), NEON_MAJOR_GRID_SPACING)};
        return mod_val < 0.5f;
    };

    // Estimate edge count for reservation.
    // Horizontal edges: (grid_size + 1) rows × grid_size edges per row.
    // Vertical edges: (grid_size + 1) columns × grid_size edges per column.
    uint32_t total_edges{2 * (config.grid_size + 1) * config.grid_size};
    result.cyan.vertices.reserve(static_cast<size_t>(total_edges) * 6);
    result.cyan.indices.reserve(static_cast<size_t>(total_edges) * 6);
    result.cyan.positions.reserve(static_cast<size_t>(total_edges) * 6);
    result.orange.vertices.reserve(static_cast<size_t>(total_edges));
    result.orange.indices.reserve(static_cast<size_t>(total_edges));
    result.orange.positions.reserve(static_cast<size_t>(total_edges));

    // Classify edges with the same OR-rule the shader uses when painting terrain
    // wireframe overlays and reflections (neonEmissiveColour). An edge goes to the
    // orange sub-mesh if its midpoint falls in orange territory — which is the case
    // whenever EITHER axis is on a major grid line. Without this alignment the tube
    // geometry is cyan while the surrounding shader-painted glow is orange at every
    // cyan-row × orange-column crossing.

    // Generate horizontal edges (along X axis, at each Z row).
    for (uint32_t z{0}; z < verts_per_side; ++z) {
        float world_z{static_cast<float>(z) * config.tile_spacing - half_size};
        bool orange_row{isOnMajorGridLine(world_z)};

        for (uint32_t x{0}; x < config.grid_size; ++x) {
            MathLib::Vec3 a{vertexPos(x, z)};
            MathLib::Vec3 b{vertexPos(x + 1, z)};
            // Midpoint classifies the whole edge (edges are short enough that either
            // endpoint's classification works). Use the starting vertex's x which
            // lies in the same "cell" as the midpoint for tile_spacing >= 1.
            float world_x_start{static_cast<float>(x) * config.tile_spacing - half_size};
            bool orange_edge{orange_row || isOnMajorGridLine(world_x_start)};
            NeonSubMesh& target{orange_edge ? result.orange : result.cyan};
            emitEdgeQuad(target, a, b);
        }
    }

    // Generate vertical edges (along Z axis, at each X column).
    for (uint32_t x{0}; x < verts_per_side; ++x) {
        float world_x{static_cast<float>(x) * config.tile_spacing - half_size};
        bool orange_col{isOnMajorGridLine(world_x)};

        for (uint32_t z{0}; z < config.grid_size; ++z) {
            MathLib::Vec3 a{vertexPos(x, z)};
            MathLib::Vec3 b{vertexPos(x, z + 1)};
            float world_z_start{static_cast<float>(z) * config.tile_spacing - half_size};
            bool orange_edge{orange_col || isOnMajorGridLine(world_z_start)};
            NeonSubMesh& target{orange_edge ? result.orange : result.cyan};
            emitEdgeQuad(target, a, b);
        }
    }

    // Compute bounding radius from all tube vertices.
    float max_dist{0.0f};
    for (const MathLib::Vec3& p : result.cyan.positions) {
        float dist{p.length()};
        if (dist > max_dist) {
            max_dist = dist;
        }
    }
    for (const MathLib::Vec3& p : result.orange.positions) {
        float dist{p.length()};
        if (dist > max_dist) {
            max_dist = dist;
        }
    }
    result.bounding_radius = max_dist;

    return result;
}
