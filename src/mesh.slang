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
    float3 light_pos; // Point light world-space position.
    float light_intensity; // Point light intensity (pre-multiplier before inverse square falloff).
};

// Must match C++ ObjectData struct layout (including padding).
struct ObjectData {
    float4x4 model;
    uint meshlet_offset;
    uint meshlet_count;
    uint pad0;
    uint pad1;
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
    float3 barycentric; // Barycentric coordinates for wireframe edge detection.
    float3 world_position; // World-space position for shadow ray origin.
    float3 world_normal; // World-space face normal for shadow ray offset.
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

// Wireframe edge colour constants.
static const float3 EDGE_COLOUR = float3(0.0, 0.8, 1.0); // Cyan neon.
static const float3 FACE_COLOUR = float3(0.02, 0.02, 0.04); // Near-black void.
static const float WIRE_WIDTH = 0.03; // Edge thickness in barycentric space.
static const float AMBIENT = 0.05; // Minimum light level (Tron glow persists even in full shadow).
static const float SELF_INTERSECTION_OFFSET = 0.002; // Normal offset to prevent self-intersection.
static const float RAY_T_MIN = 0.001; // Minimum ray distance.

// Fragment shader — Tron wireframe aesthetic with RT hard shadows.
// Point light with inverse square falloff and inline ray query shadows.
[shader("fragment")]
float4 fragMain(MeshOutput input) : SV_Target
{
    // Wireframe colour.
    float edge = min(input.barycentric.x, min(input.barycentric.y, input.barycentric.z));
    float wire = smoothstep(0.0, WIRE_WIDTH, edge);
    float3 base_colour = lerp(EDGE_COLOUR, FACE_COLOUR, wire);

    // Point light — direction and distance from surface to light.
    float3 to_light = camera.light_pos - input.world_position;
    float dist_to_light = length(to_light);
    float3 light_dir = to_light / dist_to_light;

    // Lambertian diffuse — clamp to zero for back-facing surfaces.
    float n_dot_l = max(dot(input.world_normal, light_dir), 0.0);

    // Inverse square falloff.
    float attenuation = camera.light_intensity / (dist_to_light * dist_to_light);

    float lighting = AMBIENT;

    if (n_dot_l > 0.0)
    {
        // Shadow ray — cast from surface toward the point light.
        float3 origin = input.world_position + input.world_normal * SELF_INTERSECTION_OFFSET;

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = light_dir;
        ray.TMin = RAY_T_MIN;
        ray.TMax = dist_to_light;

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
        query.TraceRayInline(scene_tlas, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, ray);
        query.Proceed();

        bool in_shadow = (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT);
        if (!in_shadow)
        {
            lighting += n_dot_l * attenuation;
        }
    }

    float3 colour = base_colour * lighting;
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

    // Load the object transform.
    float4x4 model = objects[task_payload.object_index].model;
    float4x4 mvp = mul(camera.projection, mul(camera.view, model));

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

            // Load vertex data — positions and face normal from the terrain vertex buffer.
            // Terrain uses flat shading: all 3 vertices share the same pre-computed normal.
            uint vert_base = tri_index * 3;

            for (uint v = 0; v < 3; ++v) {
                uint global_idx = meshlet_vertex_indices[meshlet.vertex_offset + local_indices[v]];
                Vertex vtx = vertices[global_idx];
                float3 pos = float3(vtx.pos_x, vtx.pos_y, vtx.pos_z);
                float3 normal = float3(vtx.norm_x, vtx.norm_y, vtx.norm_z);
                float4 world_pos = mul(model, float4(pos, 1.0));

                MeshOutput output;
                output.position = mul(mvp, float4(pos, 1.0));
                output.barycentric = BARY[v];
                output.world_position = world_pos.xyz;
                output.world_normal = normal;
                out_vertices[vert_base + v] = output;
            }

            // Triangle indices reference the duplicated vertices.
            out_triangles[tri_index] = uint3(vert_base, vert_base + 1, vert_base + 2);
        }
    }
}
