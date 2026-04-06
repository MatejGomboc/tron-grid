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

// Mesh shader — outputs one meshlet per workgroup.
// Vertices are duplicated per triangle so each gets unique barycentric
// coordinates for wireframe edge detection in the fragment shader.

struct CameraData {
    float4x4 view;
    float4x4 projection;
    float4x4 inv_view_projection; // Inverse view-projection (for skybox ray reconstruction).
    float4x4 prev_view_projection; // Previous frame's view-projection (for motion vectors).
    float3 camera_pos; // Camera world-space position (for view vector in PBR).
    uint frame_count; // Frame counter (for pseudo-random sampling).
    uint emissive_count; // Number of emissive triangles.
    float total_emissive_power; // Sum of all emissive triangle powers.
    float ubo_pad0;
    float ubo_pad1;
};

// Must match C++ ObjectData struct layout (including padding).
struct ObjectData {
    float4x4 model;
    uint meshlet_offset;
    uint meshlet_count;
    uint material_index; // Index into the material SSBO.
    uint pad1;
};

// Must match C++ Material struct layout (48 bytes, 16-byte aligned).
struct MaterialData {
    float3 base_colour;
    float roughness;
    float3 emissive;
    float emissive_strength;
    float metallic;
    float ior;
    float opacity;
    float pad;
};

struct MeshletDesc {
    uint vertex_offset;
    uint vertex_count;
    uint triangle_offset;
    uint triangle_count;
};

// Layout must match C++ Vertex struct (tightly packed, no float3 padding).
struct Vertex {
    float pos_x, pos_y, pos_z;
    float norm_x, norm_y, norm_z;
    float uv_x, uv_y;
};

// Task payload — received from the task shader.
struct TaskPayload {
    uint object_index;
    uint meshlet_offset;
    uint meshlet_count;
};

struct MeshOutput {
    float4 position : SV_Position;
    float3 barycentric : TEXCOORD0; // Barycentric coordinates for wireframe edge detection.
    float3 world_position : TEXCOORD1; // World-space position for shadow ray origin.
    nointerpolation float3 world_normal : TEXCOORD2; // World-space face normal (flat shading).
    nointerpolation uint material_index : TEXCOORD3; // Index into the material SSBO.
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraData> camera;

[[vk::binding(1, 0)]]
StructuredBuffer<ObjectData> objects;

[[vk::binding(3, 0)]]
StructuredBuffer<MeshletDesc> meshlet_descs;

[[vk::binding(4, 0)]]
StructuredBuffer<Vertex> vertices;

[[vk::binding(5, 0)]]
StructuredBuffer<uint> meshlet_vertex_indices;

[[vk::binding(6, 0)]]
StructuredBuffer<uint8_t> meshlet_triangle_indices;

[[vk::binding(7, 0)]]
RaytracingAccelerationStructure scene_tlas;

[[vk::binding(8, 0)]]
StructuredBuffer<MaterialData> materials;

// Emissive triangle for area light sampling — must match C++ EmissiveTriangle struct.
struct EmissiveTriangle {
    float3 v0;
    float area;
    float3 v1;
    float cdf;
    float3 v2;
    float pad0;
    float3 emissive;
    float pad1;
};

[[vk::binding(9, 0)]]
StructuredBuffer<EmissiveTriangle> emissive_triangles;

// ── Neon tube constants (per-fragment wireframe pattern, not per-object material) ──

// Dual-colour palette: cyan primary grid with orange accents on major grid lines.
static const float3 CYAN_EMISSIVE_COLOUR = float3(0.0, 0.8, 1.0); // Cyan neon — primary grid colour.
static const float3 ORANGE_EMISSIVE_COLOUR = float3(1.0, 0.03, 0.0); // Orange neon — accent on major grid lines.
static const float TUBE_EMISSIVE_STRENGTH = 15.0; // HDR intensity — well above 1.0.
static const float TUBE_ROUGHNESS = 1.0; // No specular — pure emissive glow.
static const float MAJOR_GRID_SPACING = 8.0; // Every Nth grid cell gets orange neon tubes.

// Wireframe edge detection — screen-space derivative-based antialiasing.
// WIRE_THICKNESS is the solid core width in pixels; WIRE_FALLOFF is the soft
// antialiased edge width in pixels. Together they produce consistent wire width
// regardless of triangle size or camera distance (Wunkolo 2022).
static const float WIRE_THICKNESS = 0.1;
static const float WIRE_FALLOFF = 0.2;
static const float REFLECTED_WIRE_WIDTH = 0.02; // Fixed barycentric-space threshold for RT reflections.

// PBR constants.
static const float PI = 3.14159265359;
static const float MIN_ROUGHNESS = 0.089; // Prevents NDF singularity and float16 denormals.
static const float3 F0_DIELECTRIC = float3(0.04, 0.04, 0.04); // Default dielectric F0 (IOR ~1.5).
static const float AMBIENT = 0.03; // Faint ambient — obsidian is nearly invisible without direct light.

// Ray tracing constants.
static const float SELF_INTERSECTION_OFFSET = 0.002; // Normal offset to prevent self-intersection.
static const float RAY_T_MIN = 0.001; // Minimum ray distance.

// ── PBR functions (Cook-Torrance BRDF) ──

// GGX / Trowbridge-Reitz normal distribution function.
// Filament's numerically stable form — avoids float16 catastrophic cancellation.
float D_GGX(float NdotH, float roughness)
{
    float a = NdotH * roughness;
    float k = roughness / (1.0 - NdotH * NdotH + a * a);
    return k * k * (1.0 / PI);
}

// Height-correlated Smith-GGX visibility function.
// Folds the 1/(4*NdotV*NdotL) denominator into the geometry term.
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
    float a2 = roughness * roughness;
    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    // Clamp denominator to prevent float16 overflow (inf) at extreme grazing angles.
    return 0.5 / max(GGXV + GGXL, 1e-4);
}

// Schlick Fresnel approximation.
float3 F_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Selects the neon tube emissive colour based on world position.
// Cells along every Nth row or column (major grid lines) glow orange;
// all other cells glow cyan. Creates a coarse orange supergrid overlay.
float3 neonEmissiveColour(float3 world_pos)
{
    float mod_x = fmod(abs(floor(world_pos.x)), MAJOR_GRID_SPACING);
    float mod_z = fmod(abs(floor(world_pos.z)), MAJOR_GRID_SPACING);
    bool is_orange = (mod_x < 0.5) || (mod_z < 0.5);
    return is_orange ? ORANGE_EMISSIVE_COLOUR : CYAN_EMISSIVE_COLOUR;
}

// ── Pseudo-random hash (PCG) ──

// PCG hash — fast, high-quality 32-bit hash.
uint pcgHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Converts a uint hash to a float in [0, 1).
float hashToFloat(uint hash)
{
    return float(hash) / 4294967296.0;
}

// Luminance of an HDR colour (BT.709 coefficients).
float luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Binary search the CDF to select an emissive triangle index.
uint searchCDF(float r, uint count)
{
    uint lo = 0;
    uint hi = count - 1;
    while (lo < hi) {
        uint mid = (lo + hi) / 2;
        if (emissive_triangles[mid].cdf < r) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

// Fragment shader — obsidian floor with neon tube edges, Cook-Torrance PBR,
// emissive area light sampling, RT shadows and reflections.
[shader("fragment")]
float4 fragMain(MeshOutput input) : SV_Target
{
    // Load material from the SSBO.
    MaterialData mat = materials[input.material_index];

    // Pure emissive objects (orb, neon tubes) — no wireframe or PBR, just emit light.
    if (mat.emissive_strength > 0.0) {
        return float4(mat.emissive * mat.emissive_strength, 1.0);
    }

    // Antialiased edge detection — screen-space derivatives give consistent pixel-width
    // wireframe regardless of triangle size. smoothstep produces a soft falloff at edges.
    float3 d_bary = fwidth(input.barycentric);
    float3 remap = smoothstep(d_bary * WIRE_THICKNESS, d_bary * (WIRE_THICKNESS + WIRE_FALLOFF), input.barycentric);
    float wire = min(remap.x, min(remap.y, remap.z));

    // Determine neon tube colour — cyan or orange based on grid position.
    float3 tube_colour = neonEmissiveColour(input.world_position);

    // Blend material properties between neon tube and surface material from SSBO.
    float3 base_colour = lerp(float3(0.0, 0.0, 0.0), mat.base_colour, wire);
    float perceptual_roughness = lerp(TUBE_ROUGHNESS, mat.roughness, wire);
    float roughness = max(perceptual_roughness * perceptual_roughness, MIN_ROUGHNESS);
    float3 emissive = lerp(tube_colour * TUBE_EMISSIVE_STRENGTH, float3(0.0, 0.0, 0.0), wire);

    // Vectors.
    // Flip normal to face the camera — two-sided lighting prevents steep terrain
    // slopes from appearing invisible when their face normal points away from the viewer.
    float3 N = normalize(input.world_normal);
    float3 V = normalize(camera.camera_pos - input.world_position);
    if (dot(N, V) < 0.0) {
        N = -N;
    }
    float NdotV = abs(dot(N, V)) + 1e-5;

    // ── Emissive area light sampling (Monte Carlo, 1 sample per pixel) ──

    float3 direct_lighting = float3(0.0, 0.0, 0.0);

    if (camera.emissive_count > 0)
    {
        // Generate pseudo-random numbers from pixel position + frame count.
        uint pixel_seed = uint(input.position.x) * 1973u + uint(input.position.y) * 9277u + camera.frame_count * 26699u;
        uint hash0 = pcgHash(pixel_seed);
        uint hash1 = pcgHash(hash0);
        uint hash2 = pcgHash(hash1);

        float r0 = hashToFloat(hash0);
        float r1 = hashToFloat(hash1);
        float r2 = hashToFloat(hash2);

        // Select emissive triangle via power-weighted CDF.
        uint tri_idx = searchCDF(r0, camera.emissive_count);
        EmissiveTriangle etri = emissive_triangles[tri_idx];

        // Uniform barycentric sampling on the selected triangle.
        float su = sqrt(r1);
        float u = 1.0 - su;
        float v = su * r2;
        float w = 1.0 - u - v;

        // Sample point on the emissive triangle.
        float3 light_point = u * etri.v0 + v * etri.v1 + w * etri.v2;

        // Direction and distance from shading point to light sample.
        float3 to_light = light_point - input.world_position;
        float dist_sq = dot(to_light, to_light);
        float dist = sqrt(dist_sq);
        float3 L = to_light / dist;

        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0)
        {
            // Light surface normal — double-sided for thin quads.
            float3 light_normal = normalize(cross(etri.v1 - etri.v0, etri.v2 - etri.v0));
            float cos_light = abs(dot(light_normal, -L));

            // Geometry term: cos_surface × cos_light / distance².
            float geometry = NdotL * cos_light / max(dist_sq, 0.0001);

            // Sampling PDF = luminance(Le) / total_power.
            float lum = luminance(etri.emissive);
            float pdf = max(lum / camera.total_emissive_power, 1e-8);

            // Shadow ray from surface toward the sampled light point.
            float3 origin = input.world_position + N * SELF_INTERSECTION_OFFSET;
            float ray_tmax = max(dist - SELF_INTERSECTION_OFFSET, RAY_T_MIN + 0.001);

            RayDesc ray;
            ray.Origin = origin;
            ray.Direction = L;
            ray.TMin = RAY_T_MIN;
            ray.TMax = ray_tmax;

            RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
            query.TraceRayInline(scene_tlas, RAY_FLAG_NONE, 0xFF, ray);
            query.Proceed();

            bool in_shadow = (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT);
            if (!in_shadow)
            {
                // Cook-Torrance BRDF for the sampled light direction.
                float3 H = normalize(V + L);
                float NdotH = clamp(dot(N, H), 0.0, 1.0);
                float VdotH = clamp(dot(V, H), 0.0, 1.0);

                float D_val = D_GGX(NdotH, roughness);
                float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
                float3 F = F_Schlick(VdotH, F0_DIELECTRIC);

                float3 spec = D_val * Vis * F;
                float3 diff = (1.0 - F) * base_colour / PI;

                // Monte Carlo estimator: BRDF × Le × geometry / pdf.
                direct_lighting = (diff + spec) * etri.emissive * geometry / pdf;
            }
        }
    }

    // ── RT reflection for obsidian surface ──

    float3 reflected_colour = float3(0.0, 0.0, 0.0);

    // Fresnel at the primary surface — controls reflection strength.
    float3 F_view = F_Schlick(max(dot(N, V), 0.0), F0_DIELECTRIC);

    // Only trace reflections for obsidian-dominant fragments (wire > 0.5).
    // Neon tube edges are emissive light sources — they glow, they do not reflect.
    if ((wire > 0.5) && (F_view.x > 0.01))
    {
        float3 reflect_dir = reflect(-V, N);
        float3 reflect_origin = input.world_position + N * SELF_INTERSECTION_OFFSET;

        RayDesc reflect_ray;
        reflect_ray.Origin = reflect_origin;
        reflect_ray.Direction = reflect_dir;
        reflect_ray.TMin = RAY_T_MIN;
        reflect_ray.TMax = 200.0;

        RayQuery<RAY_FLAG_NONE> reflect_query;
        reflect_query.TraceRayInline(scene_tlas, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, reflect_ray);
        reflect_query.Proceed();

        if (reflect_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            // Look up the material of the reflected hit via instance custom index.
            uint hit_instance = reflect_query.CommittedInstanceID();
            uint hit_mat_idx = objects[hit_instance].material_index;
            MaterialData hit_mat = materials[hit_mat_idx];

            if (hit_mat.emissive_strength > 0.0) {
                // Hit an emissive object (neon tube or orb) — return its glow directly.
                reflected_colour = hit_mat.emissive * hit_mat.emissive_strength;
            } else {
                // Hit a non-emissive surface — evaluate wireframe blend.
                float2 hit_bary = reflect_query.CommittedTriangleBarycentrics();
                float bary_w = 1.0 - hit_bary.x - hit_bary.y;
                float hit_edge = min(hit_bary.x, min(hit_bary.y, bary_w));
                float hit_wire = smoothstep(0.0, REFLECTED_WIRE_WIDTH, hit_edge);

                float3 hit_pos = reflect_origin + reflect_dir * reflect_query.CommittedRayT();
                float3 hit_tube_colour = neonEmissiveColour(hit_pos);

                float3 hit_emissive = lerp(hit_tube_colour * TUBE_EMISSIVE_STRENGTH, float3(0.0, 0.0, 0.0), hit_wire);
                float3 hit_base = lerp(float3(0.0, 0.0, 0.0), hit_mat.base_colour, hit_wire);
                reflected_colour = hit_emissive + hit_base * AMBIENT;
            }
        }
    }

    // Ambient + direct lighting + emissive + reflection.
    float3 colour = base_colour * AMBIENT + direct_lighting + emissive + reflected_colour * F_view;
    return float4(colour, 1.0);
}

// With vertex duplication for barycentrics, each triangle outputs 3 unique vertices.
// Max: 84 triangles × 3 = 252 vertices (within the 256 vertex output limit).
// Must match MAX_MESHLET_TRIANGLES in meshlet.hpp.
static const uint MAX_OUT_TRIANGLES = 84;
static const uint MAX_OUT_VERTICES = 252; // 84 × 3

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void meshMain(
    uint group_id : SV_GroupID,
    uint thread_id : SV_GroupThreadID,
    in payload TaskPayload task_payload,
    OutputVertices<MeshOutput, 252> out_vertices,
    OutputIndices<uint3, 84> out_triangles)
{
    uint meshlet_index = task_payload.meshlet_offset + group_id;
    MeshletDesc meshlet = meshlet_descs[meshlet_index];

    // Each triangle outputs 3 unique vertices (for barycentric coordinates).
    uint out_tri_count = min(meshlet.triangle_count, MAX_OUT_TRIANGLES);
    uint out_vert_count = out_tri_count * 3;
    SetMeshOutputCounts(out_vert_count, out_tri_count);

    // Load the object transform and material type.
    float4x4 model = objects[task_payload.object_index].model;
    float4x4 mvp = mul(camera.projection, mul(camera.view, model));
    uint mat_idx = objects[task_payload.object_index].material_index;

    // Barycentric coordinate lookup — vertex 0, 1, 2 of each triangle.
    static const float3 BARY[3] = {
        float3(1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0),
        float3(0.0, 0.0, 1.0)
    };

    // Each thread processes one or more triangles.
    // For each triangle, output 3 vertices with unique barycentrics.
    uint tris_per_thread = (out_tri_count + 63) / 64;
    for (uint t = 0; t < tris_per_thread; ++t) {
        uint tri_index = thread_id * tris_per_thread + t;
        if (tri_index < out_tri_count) {
            uint base = meshlet.triangle_offset + tri_index * 3;
            uint local_indices[3] = {
                uint(meshlet_triangle_indices[base + 0]),
                uint(meshlet_triangle_indices[base + 1]),
                uint(meshlet_triangle_indices[base + 2])
            };

            // Load all 3 vertices for this triangle.
            // Terrain uses flat shading: all 3 vertices share the same pre-computed normal.
            uint vert_base = tri_index * 3;

            float3 positions[3];
            float3 normals[3];
            float diagonal_flags[3];
            for (uint v = 0; v < 3; ++v) {
                uint global_idx = meshlet_vertex_indices[meshlet.vertex_offset + local_indices[v]];
                Vertex vtx = vertices[global_idx];
                positions[v] = float3(vtx.pos_x, vtx.pos_y, vtx.pos_z);
                normals[v] = float3(vtx.norm_x, vtx.norm_y, vtx.norm_z);
                diagonal_flags[v] = vtx.uv_x;
            }

            // Find which vertex is opposite the quad diagonal edge.
            // That vertex has uv_x = 1.0 (set by the terrain generator).
            // Adding its barycentric component to all 3 vertices pushes the
            // diagonal's interpolated value above the wireframe threshold,
            // effectively merging coplanar triangle pairs into quads.
            float3 diagonal_mask = float3(0.0, 0.0, 0.0);
            for (uint d = 0; d < 3; ++d) {
                if (diagonal_flags[d] > 0.5) {
                    diagonal_mask = BARY[d];
                    break;
                }
            }

            // Output vertices with diagonal-suppressed barycentrics.
            for (uint v = 0; v < 3; ++v) {
                float4 world_pos = mul(model, float4(positions[v], 1.0));

                MeshOutput output;
                output.position = mul(mvp, float4(positions[v], 1.0));
                output.barycentric = BARY[v] + diagonal_mask;
                output.world_position = world_pos.xyz;
                output.world_normal = mul((float3x3)model, normals[v]);
                output.material_index = mat_idx;
                out_vertices[vert_base + v] = output;
            }

            // Triangle indices reference the duplicated vertices.
            out_triangles[tri_index] = uint3(vert_base, vert_base + 1, vert_base + 2);
        }
    }
}
