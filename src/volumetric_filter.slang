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

// Volumetric-fog filter compute shader (Phase 8 Etape 41 sub-etapes 41b + 41c).
//
// Combines TWO variance-reduction techniques in a single per-froxel pass:
//
//  (1) Spatial 3×3 box blur in XY (per slice; Z is preserved because the slice
//      integration is exact at slice boundaries). Reduces inter-froxel variance
//      from single-frame MC by ~9×.
//
//  (2) Temporal reprojection — reconstruct this froxel's world-space centre, project
//      it through the *previous* frame's view-projection to find where it was last
//      frame, sample the previous frame's filtered grid trilinearly, and exponentially
//      blend with the current spatially-blurred value. With α = 0.1 the effective
//      sample count after convergence is ~10 frames, giving ~30× additional variance
//      reduction beyond the spatial blur.
//
// Combined with the inject's 4 samples × 9 spatial neighbours × ~10 temporal frames,
// effective sample count per froxel is ~360 — enough for visually clean fog with 17 000
// emissive triangles.
//
// Reference: Wronski 2014 "Volumetric Fog" (SIGGRAPH); Frostbite. UE/Unity HDRP use
// essentially the same pattern.
//
// Pipeline order: inject → filter (this shader) → composite.
// Ping-pong: filter both reads from history AND writes the current output, so the
// host alternates the (history, output) pair between two physical images.

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture3D<float4> froxel_inject_raw; // Read: current frame's inject output.

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture3D<float4> froxel_history; // Read: previous frame's filter output (the "history").

[[vk::binding(2, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture3D<float4> froxel_output; // Write: this frame's filter output (also next frame's history).

struct FilterPushConstants
{
    // Two mat4s up front so std430 alignment for the trailing scalars stays simple.
    float4x4 inv_view_projection; // Reconstruct world position of current froxel centre.
    float4x4 prev_view_projection; // Project that world position to previous frame's clip space.
    float3 camera_pos;
    float alpha; // EMA blend factor: out = lerp(history, current, alpha). 0.1 ≈ 10-frame history.
    uint froxel_width;
    uint froxel_height;
    uint froxel_depth;
    uint history_valid; // 0 on the very first frame, 1 thereafter.
    float near_plane;
    float far_plane;
    float pad0;
    float pad1;
};

[[vk::push_constant]]
FilterPushConstants push;

// Logarithmic depth distribution along the Z slices — same as inject_density.slang.
float froxelSliceToDepth(uint z_index)
{
    float t = (float(z_index) + 0.5) / float(push.froxel_depth);
    return push.near_plane * pow(push.far_plane / push.near_plane, t);
}

// Reconstruct the world-space position of a froxel centre. Same procedure as
// inject_density.slang's froxelToWorld (NDC unprojection → ray, scaled by view_z /
// dot(ray_dir, camera_forward) to land at the slice's view-space depth on the ray).
float3 froxelToWorld(uint3 froxel_id)
{
    float2 ndc_xy = (float2(froxel_id.xy) + 0.5) / float2(push.froxel_width, push.froxel_height) * 2.0 - 1.0;
    float view_z = froxelSliceToDepth(froxel_id.z);

    float4 near_clip = float4(ndc_xy, 0.0, 1.0);
    float4 near_world_h = mul(push.inv_view_projection, near_clip);
    float3 near_world = near_world_h.xyz / near_world_h.w;
    float3 ray_dir = normalize(near_world - push.camera_pos);

    float4 fwd_clip = float4(0.0, 0.0, 0.0, 1.0);
    float4 fwd_world_h = mul(push.inv_view_projection, fwd_clip);
    float3 fwd_world = fwd_world_h.xyz / fwd_world_h.w;
    float3 camera_forward = normalize(fwd_world - push.camera_pos);

    float ray_dot_forward = max(dot(ray_dir, camera_forward), 1e-4);
    float t = view_z / ray_dot_forward;
    return push.camera_pos + ray_dir * t;
}

// Trilinear sample of the history froxel grid at fractional coordinates. xyz are float
// froxel coordinates with origin at the (0, 0, 0) froxel centre. Edge clamp at the grid
// boundary (rather than mirror or wrap) avoids edge brightening.
float4 sampleHistoryTrilinear(float3 frx)
{
    int3 i0 = int3(floor(frx));
    int3 i1 = i0 + 1;
    float3 f = frx - float3(i0);

    int3 maxc = int3(int(push.froxel_width) - 1, int(push.froxel_height) - 1, int(push.froxel_depth) - 1);
    i0 = clamp(i0, int3(0, 0, 0), maxc);
    i1 = clamp(i1, int3(0, 0, 0), maxc);

    float4 c000 = froxel_history[uint3(i0.x, i0.y, i0.z)];
    float4 c100 = froxel_history[uint3(i1.x, i0.y, i0.z)];
    float4 c010 = froxel_history[uint3(i0.x, i1.y, i0.z)];
    float4 c110 = froxel_history[uint3(i1.x, i1.y, i0.z)];
    float4 c001 = froxel_history[uint3(i0.x, i0.y, i1.z)];
    float4 c101 = froxel_history[uint3(i1.x, i0.y, i1.z)];
    float4 c011 = froxel_history[uint3(i0.x, i1.y, i1.z)];
    float4 c111 = froxel_history[uint3(i1.x, i1.y, i1.z)];

    float4 c00 = lerp(c000, c100, f.x);
    float4 c10 = lerp(c010, c110, f.x);
    float4 c01 = lerp(c001, c101, f.x);
    float4 c11 = lerp(c011, c111, f.x);

    float4 c0 = lerp(c00, c10, f.y);
    float4 c1 = lerp(c01, c11, f.y);

    return lerp(c0, c1, f.z);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void filterMain(uint3 froxel_id : SV_DispatchThreadID)
{
    if ((froxel_id.x >= push.froxel_width) || (froxel_id.y >= push.froxel_height) || (froxel_id.z >= push.froxel_depth)) {
        return;
    }

    int max_x = int(push.froxel_width) - 1;
    int max_y = int(push.froxel_height) - 1;

    // ── Step 1: 3×3 spatial box blur of the raw inject output (this slice only) ──
    float4 spatial_sum = float4(0.0, 0.0, 0.0, 0.0);
    float spatial_weight = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int nx = int(froxel_id.x) + dx;
            int ny = int(froxel_id.y) + dy;
            if ((nx < 0) || (nx > max_x) || (ny < 0) || (ny > max_y)) {
                continue;
            }
            spatial_sum += froxel_inject_raw[uint3(uint(nx), uint(ny), froxel_id.z)];
            spatial_weight += 1.0;
        }
    }
    float4 spatial = spatial_sum / max(spatial_weight, 1.0);

    // ── Step 2: temporal reprojection ──
    // First frame has nothing in the history image — fall through with current only.
    if (push.history_valid == 0u) {
        froxel_output[froxel_id] = spatial;
        return;
    }

    // Reconstruct world-space position of THIS froxel's centre, then project it into
    // the PREVIOUS frame's clip space.
    float3 world_pos = froxelToWorld(froxel_id);
    float4 prev_clip = mul(push.prev_view_projection, float4(world_pos, 1.0));

    // Behind the previous camera (or on the near plane) — no usable history.
    if (prev_clip.w <= 1e-4) {
        froxel_output[froxel_id] = spatial;
        return;
    }

    float3 prev_ndc = prev_clip.xyz / prev_clip.w;

    // NDC xy → previous froxel xy. Vulkan's Y axis points down on screen, but the project
    // matrix doesn't flip Y here (mesh shader uses -fvk-invert-y, but compute does not),
    // so prev_ndc.y is in math convention; map +1 → froxel_y = 0 (top of screen) by
    // mirroring against the viewport.
    float prev_froxel_x = (prev_ndc.x * 0.5 + 0.5) * float(push.froxel_width);
    float prev_froxel_y = (-prev_ndc.y * 0.5 + 0.5) * float(push.froxel_height);

    // Previous view-space depth — `prev_clip.w` equals the view-space distance along the
    // forward axis (right-handed projection convention used in MathLib::makePerspective).
    float prev_view_z = prev_clip.w;

    // Off-grid in Z — outside the volumetric range (camera moved past it).
    if ((prev_view_z < push.near_plane) || (prev_view_z > push.far_plane)) {
        froxel_output[froxel_id] = spatial;
        return;
    }

    // Inverse logarithmic mapping to find the fractional Z slice the previous froxel sat in:
    // depth = near · (far/near)^t  ⇒  t = log(depth/near) / log(far/near).
    float t = log(prev_view_z / push.near_plane) / log(push.far_plane / push.near_plane);
    float prev_froxel_z = t * float(push.froxel_depth) - 0.5;

    // Reject if previous froxel projected outside the screen-aligned tile grid.
    if ((prev_froxel_x < 0.0) || (prev_froxel_x >= float(push.froxel_width)) || (prev_froxel_y < 0.0) || (prev_froxel_y >= float(push.froxel_height))
        || (prev_froxel_z < 0.0) || (prev_froxel_z >= float(push.froxel_depth))) {
        froxel_output[froxel_id] = spatial;
        return;
    }

    // Trilinear sample of the history at the reprojected coordinates.
    float4 history = sampleHistoryTrilinear(float3(prev_froxel_x, prev_froxel_y, prev_froxel_z));

    // EMA blend: out = lerp(history, current, alpha). Lower alpha = longer-tail history,
    // smoother but slower to react to changes.
    float4 result = lerp(history, spatial, push.alpha);

    froxel_output[froxel_id] = result;
}
