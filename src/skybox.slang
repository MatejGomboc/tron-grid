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

// Procedural cyberpunk skybox — faintly glowing cyan-green data fog clouds drifting
// through infinite darkness. Renders as a fullscreen triangle with depth test
// (eLessOrEqual at z = 1.0) so only sky pixels (depth = 1.0) are shaded.
//
// The procedural sky function itself lives in skybox_common.slang so the same code path
// is shared with mesh.slang's reflection-ray miss handler — see skybox_common.slang for
// the noise + horizon-gradient + cloud-threshold logic.

import skybox_common;

struct CameraData {
    float4x4 view;
    float4x4 projection;
    float4x4 inv_view_projection;
    float4x4 prev_view_projection;
    float3 camera_pos;
    uint frame_count;
    uint emissive_count;
    float total_emissive_power;
    uint screen_width;
    uint screen_height;
    float time; //!< Elapsed time in seconds (drives cloud-drift animation).
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraData> camera;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

//! Fullscreen triangle vertex shader — generates a screen-filling triangle from vertex ID.
[shader("vertex")]
VSOutput skyVertMain(uint vertex_id : SV_VertexID)
{
    // Fullscreen triangle: 3 vertices cover the entire screen.
    // Vertex 0: (-1, -1), Vertex 1: (3, -1), Vertex 2: (-1, 3)
    // GPU clips the triangle to the viewport — no wasted bandwidth.
    float2 pos = float2((vertex_id << 1) & 2, vertex_id & 2);
    float2 uv = pos;
    pos = pos * 2.0 - 1.0;

    VSOutput output;
    output.position = float4(pos, 1.0, 1.0);
    output.uv = uv;
    return output;
}

//! Procedural cyberpunk sky fragment shader — data fog clouds at the horizon.
[shader("fragment")]
float4 skyFragMain(VSOutput input) : SV_Target
{
    // Reconstruct world-space ray direction from screen UV.
    // No manual Y-flip — slangc -fvk-invert-y flips SV_Position, which causes the
    // vertex whose original (math) pos was (-1, -1) to land at the Vulkan *bottom*
    // screen pixel, carrying its original UV=(0,0). The UV at the *top* Vulkan pixel
    // is therefore interpolated as (_, 1), so ndc = uv*2-1 already has Y=+1 at top,
    // which is the math convention the CPU-computed inv_view_projection expects.
    float2 ndc = input.uv * 2.0 - 1.0;

    float4 world_pos = mul(camera.inv_view_projection, float4(ndc, 1.0, 1.0));
    float3 ray_dir = normalize(world_pos.xyz / world_pos.w - camera.camera_pos);

    return float4(sampleProceduralSky(ray_dir, camera.time), 1.0);
}
