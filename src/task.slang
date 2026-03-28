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

struct CameraData {
    float4x4 view;
    float4x4 projection;
};

struct ObjectData {
    float4x4 model;
};

struct ObjectBounds {
    float3 centre;
    float radius;
};

// Task payload — passed from task shader to mesh shader.
struct TaskPayload {
    uint object_index;    // Which object this batch belongs to.
    uint meshlet_offset;  // First meshlet index for this object's mesh.
    uint meshlet_count;   // Number of meshlets to render.
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraData> camera;

[[vk::binding(1, 0)]]
StructuredBuffer<ObjectData> objects;

[[vk::binding(2, 0)]]
StructuredBuffer<ObjectBounds> bounds;

// Push constants — frustum planes + object/meshlet counts.
struct TaskPushConstants {
    float4 planes[6];
    uint object_count;
    uint meshlets_per_object;  // Number of meshlets per mesh (uniform for now).
    uint meshlet_offset;       // Global meshlet offset for this mesh type.
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

    // Frustum test — use world-space bounds against world-space frustum planes.
    if (!isVisible(obj_bounds.centre, obj_bounds.radius)) {
        return;
    }

    // Object is visible — dispatch mesh shader workgroups for its meshlets.
    TaskPayload payload;
    payload.object_index = object_index;
    payload.meshlet_offset = push.meshlet_offset;
    payload.meshlet_count = push.meshlets_per_object;

    DispatchMesh(push.meshlets_per_object, 1, 1, payload);
}
