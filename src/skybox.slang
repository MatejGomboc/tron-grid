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

// Procedural cyberpunk skybox — faintly glowing cyan-green data fog clouds
// drifting through infinite darkness. Renders as a fullscreen triangle with
// depth test (eLessOrEqual) so only sky pixels (depth = 1.0) are shaded.
// Value noise (3 octaves) on the world-space ray direction produces slow-rolling
// volumetric cloud shapes concentrated at the horizon.

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
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraData> camera;

struct SkyboxPushConstants
{
    float time;
};

[[vk::push_constant]]
SkyboxPushConstants push;

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// ── Value noise (ported from terrain.cpp) ──

//! Hash function — produces a pseudo-random value in [0, 1] from integer coordinates.
float hashNoise(int2 p, uint seed)
{
    uint h = uint(p.x) * 374761393u + uint(p.y) * 668265263u + seed * 1274126177u;
    h = (h ^ (h >> 13)) * 1103515245u;
    h = h ^ (h >> 16);
    return float(h & 0x7FFFFFFF) / float(0x7FFFFFFF);
}

//! 2D value noise with smoothstep interpolation.
float valueNoise(float2 coord, uint seed)
{
    int2 i = int2(floor(coord));
    float2 f = frac(coord);

    // Smoothstep interpolation.
    f = f * f * (3.0 - 2.0 * f);

    float v00 = hashNoise(i + int2(0, 0), seed);
    float v10 = hashNoise(i + int2(1, 0), seed);
    float v01 = hashNoise(i + int2(0, 1), seed);
    float v11 = hashNoise(i + int2(1, 1), seed);

    float v0 = lerp(v00, v10, f.x);
    float v1 = lerp(v01, v11, f.x);
    return lerp(v0, v1, f.y);
}

//! Layered value noise (octaves) for cloud density.
float layeredNoise(float2 coord, uint octaves, float frequency, uint seed)
{
    float value = 0.0;
    float amplitude = 1.0;
    float total_amplitude = 0.0;
    float freq = frequency;

    for (uint i = 0; i < octaves; ++i) {
        value += amplitude * valueNoise(coord * freq, seed + i);
        total_amplitude += amplitude;
        amplitude *= 0.5;
        freq *= 2.0;
    }

    return value / total_amplitude;
}

// ── Sky constants ──

static const float3 SKY_BASE_COLOUR = float3(0.002, 0.005, 0.004);
static const float3 CLOUD_COLOUR = float3(0.0, 0.04, 0.03);
static const float CLOUD_FREQUENCY = 0.8;
static const uint CLOUD_OCTAVES = 3;
static const uint CLOUD_SEED = 42;
static const float CLOUD_SPEED = 0.01;
static const float CLOUD_DENSITY_THRESHOLD = 0.45;
static const float CLOUD_DENSITY_SOFTNESS = 0.2;

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

    // Horizon density gradient — clouds concentrate at the horizon, sparse overhead.
    float horizon_factor = 1.0 - abs(ray_dir.y);
    horizon_factor = horizon_factor * horizon_factor;

    // Sample layered value noise on the ray direction (XZ plane) with time scroll.
    float2 noise_coord = ray_dir.xz / max(abs(ray_dir.y) + 0.1, 0.1);
    noise_coord += float2(push.time * CLOUD_SPEED, push.time * CLOUD_SPEED * 0.7);

    float density = layeredNoise(noise_coord, CLOUD_OCTAVES, CLOUD_FREQUENCY, CLOUD_SEED);

    // Shape the density with a soft threshold.
    density = smoothstep(CLOUD_DENSITY_THRESHOLD, CLOUD_DENSITY_THRESHOLD + CLOUD_DENSITY_SOFTNESS, density);
    density *= horizon_factor;

    // Blend between dark sky base and brighter cloud wisps.
    float3 colour = lerp(SKY_BASE_COLOUR, CLOUD_COLOUR, density);

    return float4(colour, 1.0);
}
