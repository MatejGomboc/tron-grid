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

// Task shader — per-object frustum culling, dispatches mesh shader workgroups.
// One task shader workgroup processes one object. If the object is visible,
// it dispatches mesh shader workgroups for each of the object's meshlets.
// Meshlet offset/count are per-object (stored in ObjectData SSBO).

// Layout MUST match the C++ CameraUBO struct in pipeline.hpp byte-for-byte; the same
// UBO is bound to descriptor 0 in mesh.slang and skybox.slang and they share the buffer
// view. Task shader does not read `time`, but the field is declared here so all shader
// views of the buffer agree on its byte layout.
struct CameraData {
    float4x4 view;
    float4x4 projection;
    float4x4 inv_view_projection; // Inverse view-projection (for skybox ray reconstruction).
    float4x4 prev_view_projection; // Previous frame's view-projection (for motion vectors).
    float3 camera_pos; // Camera world-space position (for view vector in PBR).
    uint frame_count; // Frame counter (for pseudo-random sampling).
    uint emissive_count; // Number of emissive triangles.
    float total_emissive_power; // Sum of all emissive triangle powers.
    uint screen_width; // Render target width in pixels.
    uint screen_height; // Render target height in pixels.
    float time; // Elapsed seconds (read by mesh / skybox shaders for sky cloud drift).
};

// Must match C++ ObjectData struct layout (including padding).
struct ObjectData {
    float4x4 model;
    uint meshlet_offset;
    uint meshlet_count;
    uint material_index; // Index into the material SSBO.
    uint pad1;
};

struct ObjectBounds {
    float3 centre;
    float radius;
};

// Task payload — passed from task shader to mesh shader.
struct TaskPayload {
    uint object_index;
    uint meshlet_offset;
    uint meshlet_count;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraData> camera;

[[vk::binding(1, 0)]]
StructuredBuffer<ObjectData> objects;

[[vk::binding(2, 0)]]
StructuredBuffer<ObjectBounds> bounds;

// Push constants — frustum planes + object range. The range [base_object_index,
// base_object_index + object_count) lets a single dispatch render any contiguous
// slice of the object SSBO. Used for opaque-vs-transparent partitioning: opaque
// dispatch sets base = 0, count = N_opaque; transparent dispatch sets base =
// N_opaque, count = N_transparent. Layout MUST match TaskPushConstants in
// pipeline.hpp byte-for-byte.
struct TaskPushConstants {
    float4 planes[6];
    uint object_count;
    uint base_object_index;
};

[[vk::push_constant]]
TaskPushConstants push;

// Frustum test — returns true if the bounding sphere is at least partially inside.
bool isVisible(float3 centre, float radius)
{
    for (uint i = 0; i < 6; ++i) {
        float distance = dot(push.planes[i].xyz, centre) + push.planes[i].w;
        if (distance < -radius) {
            return false;
        }
    }
    return true;
}

[shader("amplification")]
[numthreads(1, 1, 1)]
void taskMain(uint3 dispatch_id : SV_DispatchThreadID)
{
    if (dispatch_id.x >= push.object_count) {
        return;
    }

    // Apply the dispatch base offset so that an opaque-only dispatch with
    // base=0 / count=N_opaque processes entities [0, N_opaque), and a
    // transparent dispatch with base=N_opaque / count=N_transparent processes
    // entities [N_opaque, N_opaque + N_transparent).
    uint object_index = push.base_object_index + dispatch_id.x;

    // Bounds are already in world space — no transform needed.
    ObjectBounds obj_bounds = bounds[object_index];

    // Frustum test.
    if (!isVisible(obj_bounds.centre, obj_bounds.radius)) {
        return;
    }

    // Object is visible — read per-object meshlet info and dispatch.
    ObjectData obj = objects[object_index];

    TaskPayload payload;
    payload.object_index = object_index;
    payload.meshlet_offset = obj.meshlet_offset;
    payload.meshlet_count = obj.meshlet_count;

    DispatchMesh(obj.meshlet_count, 1, 1, payload);
}
