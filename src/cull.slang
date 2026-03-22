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

// Per-object bounding sphere — matches C++ ObjectBounds struct.
struct ObjectBounds {
    float3 centre;
    float radius;
};

// Indirect draw command — matches VkDrawIndexedIndirectCommand.
struct DrawCommand {
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

// Frustum planes passed via push constants (6 planes × float4 = 96 bytes).
struct CullPushConstants {
    float4 planes[6];
    uint object_count;
};

[[vk::push_constant]]
CullPushConstants cull;

// Object bounds (read-only).
[[vk::binding(0, 0)]]
StructuredBuffer<ObjectBounds> bounds;

// Indirect draw command (read-write — we modify instance_count).
[[vk::binding(1, 0)]]
RWStructuredBuffer<DrawCommand> draw_commands;

// Draw count buffer (read-write — we increment the draw count).
[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> draw_count;

// Compacted visible object indices (read-write — visible objects write their index here).
[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> visible_indices;

// Tests whether a bounding sphere is inside the frustum (all 6 planes).
bool isVisible(float3 centre, float radius)
{
    for (int i = 0; i < 6; ++i) {
        float distance = dot(cull.planes[i].xyz, centre) + cull.planes[i].w;
        if (distance < -radius) {
            return false;
        }
    }
    return true;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void cullMain(uint3 thread_id : SV_DispatchThreadID)
{
    uint object_index = thread_id.x;
    if (object_index >= cull.object_count) {
        return;
    }

    ObjectBounds obj = bounds[object_index];
    if (isVisible(obj.centre, obj.radius)) {
        // Atomically increment the instance count in the draw command
        uint visible_slot;
        InterlockedAdd(draw_commands[0].instance_count, 1, visible_slot);

        // Write the object index to the compacted visible indices buffer
        visible_indices[visible_slot] = object_index;
    }
}
