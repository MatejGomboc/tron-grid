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

// Volumetric-fog composite compute shader (Phase 8 Etape 41 sub-etape 41a).
//
// For each HDR pixel: compute its view-space depth (currently approximated as the
// far plane — 41c will sample the actual depth buffer for proper occlusion), march
// through the froxel grid along that pixel's view ray, accumulate scattered radiance
// and transmittance, then composite onto the existing HDR colour:
//
//     final = scene * transmittance + scattered
//
// Per Wronski 2014 / Frostbite "Adaptive Volumetric Shadow Maps" the per-slice
// integration is:
//
//     T_slice  = exp(-extinction_slice)         // transmittance through this slice
//     L_total += L_slice * T_accum              // scattered radiance accumulated so far
//     T_accum *= T_slice                        // attenuate accumulated transmittance
//
// 41a has zero in-scattered radiance everywhere (L_slice = 0), so the shader's only
// visible effect is uniform depth-darkening of the scene. 41b adds per-froxel emissive
// sampling so the scattered term becomes non-zero.

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> hdr_image;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture3D<float4> froxel_grid; // Read-only here; RW because the inject pass also binds it as RW.

struct VolumetricPushConstants
{
    uint screen_width;
    uint screen_height;
    uint froxel_width;
    uint froxel_height;
    uint froxel_depth;
    float pad0;
    float pad1;
    float pad2;
};

[[vk::push_constant]]
VolumetricPushConstants push;

// Manual bilinear interpolation in the X/Y froxel plane. The froxel grid is 160×90
// while the screen is up to 4K; nearest-neighbour read produces ugly 8×8-pixel "90s game"
// fog blocks. Bilinear smooths across froxel boundaries at the cost of 4 SSBO reads per
// slice instead of 1 — composite is still well under 1 ms on the RTX 4090. Wronski 2014
// and Frostbite both filter the froxel read; we replicate that here.
//
// (Z stays discrete because the slice integration is exactly the Frostbite recurrence
// `T_slice = exp(-σ_t·dz); L += L_slice·T_accum; T_accum *= T_slice` — interpolating
// between slices would change the integration model.)
float4 sampleFroxelBilinear(float2 froxel_xy, uint z)
{
    int2 i0 = int2(floor(froxel_xy - 0.5));
    int2 i1 = i0 + 1;
    float2 frac_xy = (froxel_xy - 0.5) - float2(i0);

    int max_x = int(push.froxel_width) - 1;
    int max_y = int(push.froxel_height) - 1;
    i0 = clamp(i0, int2(0, 0), int2(max_x, max_y));
    i1 = clamp(i1, int2(0, 0), int2(max_x, max_y));

    float4 c00 = froxel_grid[uint3(i0.x, i0.y, z)];
    float4 c10 = froxel_grid[uint3(i1.x, i0.y, z)];
    float4 c01 = froxel_grid[uint3(i0.x, i1.y, z)];
    float4 c11 = froxel_grid[uint3(i1.x, i1.y, z)];

    float4 c0 = lerp(c00, c10, frac_xy.x);
    float4 c1 = lerp(c01, c11, frac_xy.x);
    return lerp(c0, c1, frac_xy.y);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compositeMain(uint3 thread_id : SV_DispatchThreadID)
{
    if ((thread_id.x >= push.screen_width) || (thread_id.y >= push.screen_height)) {
        return;
    }

    uint2 pixel = thread_id.xy;

    // March through the froxel grid along this pixel's column. The froxel grid is
    // frustum-aligned: the (x, y) tile for this pixel is the same x/y throughout the
    // depth slices.
    float2 froxel_xy = (float2(pixel) + 0.5) / float2(push.screen_width, push.screen_height) * float2(push.froxel_width, push.froxel_height);

    float3 accumulated_radiance = float3(0.0, 0.0, 0.0);
    float accumulated_transmittance = 1.0;

    for (uint z = 0; z < push.froxel_depth; ++z) {
        float4 slice = sampleFroxelBilinear(froxel_xy, z);
        float3 in_scatter = slice.rgb;
        float extinction = slice.a;

        float slice_transmittance = exp(-extinction);
        accumulated_radiance += in_scatter * accumulated_transmittance;
        accumulated_transmittance *= slice_transmittance;
    }

    // Composite onto the existing HDR pixel.
    float4 scene = hdr_image[pixel];
    float3 fogged = scene.rgb * accumulated_transmittance + accumulated_radiance;
    hdr_image[pixel] = float4(fogged, scene.a);
}
