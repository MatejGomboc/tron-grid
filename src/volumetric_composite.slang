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
    uint froxel_x = uint((float(pixel.x) / float(push.screen_width)) * float(push.froxel_width));
    uint froxel_y = uint((float(pixel.y) / float(push.screen_height)) * float(push.froxel_height));
    froxel_x = min(froxel_x, push.froxel_width - 1);
    froxel_y = min(froxel_y, push.froxel_height - 1);

    float3 accumulated_radiance = float3(0.0, 0.0, 0.0);
    float accumulated_transmittance = 1.0;

    for (uint z = 0; z < push.froxel_depth; ++z) {
        float4 slice = froxel_grid[uint3(froxel_x, froxel_y, z)];
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
