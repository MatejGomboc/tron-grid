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
// Reads vertex positions and indices from SSBOs, transforms vertices
// by the per-object model matrix, and outputs triangles.

struct CameraData {
    float4x4 view;
    float4x4 projection;
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
// float3 in std430 would be padded to 16 bytes; using explicit fields avoids this.
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
    float3 colour;
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

// Fragment shader — outputs normal as colour (same as before).
[shader("fragment")]
float4 fragMain(MeshOutput input) : SV_Target
{
    return float4(input.colour * 0.5 + 0.5, 1.0);
}

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void meshMain(
    uint group_id : SV_GroupID,
    uint thread_id : SV_GroupThreadID,
    in payload TaskPayload task_payload,
    OutputVertices<MeshOutput, 64> out_vertices,
    OutputIndices<uint3, 124> out_triangles)
{
    uint meshlet_index = task_payload.meshlet_offset + group_id;
    MeshletDesc meshlet = meshlet_descs[meshlet_index];

    SetMeshOutputCounts(meshlet.vertex_count, meshlet.triangle_count);

    // Load the object transform.
    float4x4 model = objects[task_payload.object_index].model;
    float4x4 mvp = mul(camera.projection, mul(camera.view, model));

    // Output vertices — one thread per vertex (up to 64).
    if (thread_id < meshlet.vertex_count) {
        uint global_vertex_index = meshlet_vertex_indices[meshlet.vertex_offset + thread_id];
        Vertex v = vertices[global_vertex_index];

        float3 pos = float3(v.pos_x, v.pos_y, v.pos_z);
        float3 normal = float3(v.norm_x, v.norm_y, v.norm_z);

        MeshOutput output;
        output.position = mul(mvp, float4(pos, 1.0));
        // Normal visualisation — transform by model matrix (uniform scale only).
        float3 world_normal = mul((float3x3)model, normal);
        output.colour = normalize(world_normal);
        out_vertices[thread_id] = output;
    }

    // Output triangles — one thread per triangle (up to 124, but we have 64 threads).
    // Each thread may output multiple triangles.
    uint triangles_per_thread = (meshlet.triangle_count + 63) / 64;
    for (uint t = 0; t < triangles_per_thread; ++t) {
        uint tri_index = thread_id * triangles_per_thread + t;
        if (tri_index < meshlet.triangle_count) {
            uint base = meshlet.triangle_offset + tri_index * 3;
            uint i0 = uint(meshlet_triangle_indices[base + 0]);
            uint i1 = uint(meshlet_triangle_indices[base + 1]);
            uint i2 = uint(meshlet_triangle_indices[base + 2]);
            out_triangles[tri_index] = uint3(i0, i1, i2);
        }
    }
}
