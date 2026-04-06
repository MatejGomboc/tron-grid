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
    uint material_type; // 0 = terrain (PBR obsidian + neon), 1 = emissive orb.
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

// Push constants — frustum planes + total object count.
struct TaskPushConstants {
    float4 planes[6];
    uint object_count;
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
    uint object_index = dispatch_id.x;
    if (object_index >= push.object_count) {
        return;
    }

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
