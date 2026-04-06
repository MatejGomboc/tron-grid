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
    float3 light_pos; // Point light world-space position.
    float light_intensity; // Point light intensity (pre-multiplier before inverse square falloff).
    float3 camera_pos; // Camera world-space position (for view vector in PBR).
    float camera_pad;
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

// Fragment shader — obsidian floor with neon tube edges, Cook-Torrance PBR, RT shadows.
[shader("fragment")]
float4 fragMain(MeshOutput input) : SV_Target
{
    // Load material from the SSBO.
    MaterialData mat = materials[input.material_index];

    // Pure emissive objects (orb) — no wireframe or PBR, just emit light.
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
    float3 to_light = camera.light_pos - input.world_position;
    float dist_to_light = length(to_light);
    float3 L = to_light / dist_to_light;
    float3 H = normalize(V + L);

    // Dot products with safety guards.
    float NdotV = abs(dot(N, V)) + 1e-5;
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = clamp(dot(N, H), 0.0, 1.0);
    float VdotH = clamp(dot(V, H), 0.0, 1.0);

    // Inverse square falloff.
    float attenuation = camera.light_intensity / max(dist_to_light * dist_to_light, 0.0001);

    // Cook-Torrance BRDF.
    float  D = D_GGX(NdotH, roughness);
    float  Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
    float3 F = F_Schlick(VdotH, F0_DIELECTRIC);

    float3 specular = D * Vis * F;
    float3 diffuse = (1.0 - F) * base_colour / PI;

    float3 direct_lighting = float3(0.0, 0.0, 0.0);

    if (NdotL > 0.0)
    {
        // Shadow ray — cast from surface toward the point light.
        float3 origin = input.world_position + N * SELF_INTERSECTION_OFFSET;

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = L;
        ray.TMin = RAY_T_MIN;
        ray.TMax = dist_to_light;

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
        query.TraceRayInline(scene_tlas, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, ray);
        query.Proceed();

        bool in_shadow = (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT);
        if (!in_shadow)
        {
            float3 radiance = float3(1.0, 1.0, 1.0) * attenuation;
            direct_lighting = (diffuse + specular) * radiance * NdotL;
        }
    }

    // ── RT reflection for obsidian surface ──

    float3 reflected_colour = float3(0.0, 0.0, 0.0);

    // Fresnel at the primary surface — controls reflection strength.
    float3 F_view = F_Schlick(max(dot(N, V), 0.0), F0_DIELECTRIC);

    // Only trace reflections for obsidian-dominant fragments (wire > 0.5).
    // Neon tube edges are emissive light sources — they glow, they do not reflect.
    if (wire > 0.5 && F_view.x > 0.01)
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
            // Evaluate wireframe edge detection at the reflected hit point.
            // hit_bary = (u, v), with w = 1 - u - v.
            // NOTE: these are raw hardware barycentrics — the diagonal suppression
            // applied in the mesh shader output is not available here, so reflected
            // wireframe may show quad diagonals that the direct view suppresses.
            float2 hit_bary = reflect_query.CommittedTriangleBarycentrics();
            float bary_w = 1.0 - hit_bary.x - hit_bary.y;
            float hit_edge = min(hit_bary.x, min(hit_bary.y, bary_w));
            // Fixed barycentric-space threshold for reflections — fwidth is unavailable
            // in ray query code. The reflected wireframe is small and blurred by the
            // reflection, so a fixed threshold is acceptable here.
            float hit_wire = smoothstep(0.0, REFLECTED_WIRE_WIDTH, hit_edge);

            // Compute world-space position of the reflected hit for neon colour lookup.
            float3 hit_pos = reflect_origin + reflect_dir * reflect_query.CommittedRayT();
            float3 hit_tube_colour = neonEmissiveColour(hit_pos);

            // Evaluate material at the reflected hit point.
            float3 hit_emissive = lerp(hit_tube_colour * TUBE_EMISSIVE_STRENGTH, float3(0.0, 0.0, 0.0), hit_wire);
            // Reflected hits use the current object's material for the surface colour.
            // This is approximate — ideally we would look up the hit object's material.
            float3 hit_base = lerp(float3(0.0, 0.0, 0.0), mat.base_colour, hit_wire);

            // The reflected colour is primarily the emissive neon tubes.
            // Obsidian faces reflect as near-black (which is correct — dark stone).
            reflected_colour = hit_emissive + hit_base * AMBIENT;
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
