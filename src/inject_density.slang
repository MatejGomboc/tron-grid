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

// Density-injection compute shader (Phase 8 Etape 41 sub-etape 41a).
//
// Writes per-froxel scattering data into a 3D image (frustum-aligned voxel grid):
//   .rgb = in-scattered radiance (held at zero in 41a; 41b adds emissive sampling)
//   .a   = extinction coefficient × slice thickness (so the composite raymarch can
//          accumulate transmittance via a simple sum / exp instead of a per-step
//          density × dz multiply)
//
// The grid is logically laid out as (FROXEL_X, FROXEL_Y, FROXEL_Z) where:
//   - X / Y are frustum-aligned (x-y froxel index = screen-space tile)
//   - Z slices along the view direction with a logarithmic depth distribution
//     (more resolution near the camera, less at the far plane), matching the
//     Frostbite / UE / Unity HDRP convention.
//
// 41a uses a height-falloff density: thicker near the ground (y < FOG_HEIGHT_BASE),
// thinner above. Constant in x/z. No light injection yet.

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture3D<float4> froxel_grid;

struct FroxelPushConstants
{
    uint froxel_width;
    uint froxel_height;
    uint froxel_depth;
    float fog_density; // Base extinction (m^-1) at ground level.
    float fog_height_base; // World-space y where fog density starts to fall off.
    float fog_height_falloff; // Exponential falloff scale (metres).
    float near_plane;
    float far_plane;
    float4x4 inv_view_projection; // For unprojecting froxel centres back to world space.
    float3 camera_pos;
    float pad;
};

[[vk::push_constant]]
FroxelPushConstants push;

// Logarithmic depth distribution along the Z slices — packs more resolution close to the
// camera. depth_at_slice(0) = near_plane, depth_at_slice(Z-1) ≈ far_plane.
float froxelSliceToDepth(uint z_index)
{
    float t = (float(z_index) + 0.5) / float(push.froxel_depth);
    return push.near_plane * pow(push.far_plane / push.near_plane, t);
}

// Inverse: world depth → slice thickness in the linear z domain so we can integrate the
// extinction × dz contribution per froxel.
float froxelSliceThickness(uint z_index)
{
    float t0 = float(z_index) / float(push.froxel_depth);
    float t1 = float(z_index + 1) / float(push.froxel_depth);
    float z0 = push.near_plane * pow(push.far_plane / push.near_plane, t0);
    float z1 = push.near_plane * pow(push.far_plane / push.near_plane, t1);
    return z1 - z0;
}

// Reconstruct the world-space position of a froxel centre from its (x, y, z) integer
// coordinates. Uses the inverse view-projection matrix to walk from screen-space NDC at
// the slice's depth back into world coordinates. (For 41a we only need the world Y for
// the height-falloff density, but the full reconstruction must be correct so 41b's
// per-froxel emissive sampling and shadow rays land at the right world positions.)
//
// Math: froxelSliceToDepth gives view_z, the linear depth along **camera_forward**
// (the slice's depth in view space). The view ray for an off-axis NDC point is at
// some angle to camera_forward, so advancing by `view_z` along `ray_dir` would land
// the result at view depth `view_z · cos(angle)` < view_z (closer than intended) at
// corner pixels. Correct: parametric distance along the ray equals view_z divided
// by the cosine of the angle between ray and forward, i.e. `t = view_z /
// dot(ray_dir, camera_forward)`.
float3 froxelToWorld(uint3 froxel_id)
{
    float2 ndc_xy = (float2(froxel_id.xy) + 0.5) / float2(push.froxel_width, push.froxel_height) * 2.0 - 1.0;
    float view_z = froxelSliceToDepth(froxel_id.z);

    // Unproject the NDC point at the near plane to get a world-space ray direction.
    float4 near_clip = float4(ndc_xy, 0.0, 1.0);
    float4 near_world_h = mul(push.inv_view_projection, near_clip);
    float3 near_world = near_world_h.xyz / near_world_h.w;
    float3 ray_dir = normalize(near_world - push.camera_pos);

    // Compute camera_forward by unprojecting the NDC centre at the near plane and
    // subtracting the camera position. (Per-froxel cost is one mul + one normalise;
    // negligible against the 921k-froxel total. Avoids adding a separate camera_forward
    // push-constant member.)
    float4 fwd_clip = float4(0.0, 0.0, 0.0, 1.0);
    float4 fwd_world_h = mul(push.inv_view_projection, fwd_clip);
    float3 fwd_world = fwd_world_h.xyz / fwd_world_h.w;
    float3 camera_forward = normalize(fwd_world - push.camera_pos);

    float ray_dot_forward = max(dot(ray_dir, camera_forward), 1e-4);
    float t = view_z / ray_dot_forward;
    return push.camera_pos + ray_dir * t;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void injectMain(uint3 froxel_id : SV_DispatchThreadID)
{
    if ((froxel_id.x >= push.froxel_width) || (froxel_id.y >= push.froxel_height) || (froxel_id.z >= push.froxel_depth)) {
        return;
    }

    float3 world_pos = froxelToWorld(froxel_id);

    // Height-falloff density: full strength at and below fog_height_base, exponentially
    // attenuated above. Models a ground-hugging fog layer that's denser near the Tron
    // grid floor than up in the sky.
    float height_above = max(world_pos.y - push.fog_height_base, 0.0);
    float density = push.fog_density * exp(-height_above / push.fog_height_falloff);

    // Per-froxel slice thickness in world units → extinction-along-this-slice for the
    // composite raymarch. (Optical depth per slice = density × slice_thickness.)
    float slice_dz = froxelSliceThickness(froxel_id.z);
    float extinction = density * slice_dz;

    // 41a: no emissive in-scattering yet, only extinction. Ground-level fog will dim
    // the scene in proportion to depth. 41b adds per-froxel emissive sampling here.
    froxel_grid[froxel_id] = float4(0.0, 0.0, 0.0, extinction);
}
