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

// Density-injection compute shader (Phase 8 Etape 41 sub-etape 41b).
//
// Writes per-froxel scattering data into a 3D image (frustum-aligned voxel grid):
//   .rgb = in-scattered radiance accumulated at this froxel toward the camera (Wronski
//          2014 / Frostbite). 41a left this at zero; 41b samples one emissive triangle
//          per froxel via the same power-weighted CDF used for ReSTIR DI, traces a
//          shadow ray from the froxel centre to the sample point, and accumulates the
//          emissive radiance modulated by the geometric term and a Henyey-Greenstein
//          phase function (g = hg_g, typically 0.6 for atmospheric haze).
//   .a   = optical depth = extinction × slice thickness. The composite raymarch builds
//          per-slice transmittance from this via exp(-.a) and accumulates against the
//          rgb in-scatter.
//
// The grid is logically laid out as (FROXEL_X, FROXEL_Y, FROXEL_Z) where:
//   - X / Y are frustum-aligned (x-y froxel index = screen-space tile)
//   - Z slices along the view direction with a logarithmic depth distribution
//     (more resolution near the camera, less at the far plane), matching the
//     Frostbite / UE / Unity HDRP convention.
//
// Density still uses the 41a height-falloff model: thicker near the ground
// (y < FOG_HEIGHT_BASE), exponentially thinner above. Constant in x/z.

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture3D<float4> froxel_grid;

[[vk::binding(1, 0)]]
RaytracingAccelerationStructure tlas; // Visibility shadow rays from froxel centre to light sample.

// Emissive triangle entry — must match the C++ EmissiveTriangle struct in pipeline.hpp
// and the Slang EmissiveTriangle struct in mesh.slang byte-for-byte (std430).
struct EmissiveTriangle
{
    float3 v0;
    float area;
    float3 v1;
    float cdf; // Power-weighted cumulative distribution.
    float3 v2;
    float pad0;
    float3 emissive; // Radiance = colour × strength.
    float pad1;
};

[[vk::binding(2, 0)]]
StructuredBuffer<EmissiveTriangle> emissive_triangles;

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
    uint frame_count; // Per-frame jitter seed (rotates the random sample selection).
    uint emissive_count; // Number of emissive triangles in the SSBO.
    float total_emissive_power; // Sum of all emissive triangle powers (PDF normalisation).
    float hg_g; // Henyey-Greenstein asymmetry parameter g ∈ (-1, 1).
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
    // Map froxel xy index → math-convention NDC. Project's projection matrix is
    // math-convention (+Y = up); the mesh shader's `-fvk-invert-y` slangc flag flips
    // SV_Position only at the rasterisation stage. Compute shaders never see that flip.
    // The composite reads froxel index y=0 for screen pixel y=0 (Vulkan top of screen),
    // which in math NDC is +1 (top of math frustum). So froxel index y=0 must produce
    // ndc.y = +1 (not -1, which would be bottom of math frustum and would display the
    // sampled fog data at the bottom of the screen — vertically mirrored). Pre-Etape
    // 42c-polish-2 the formula was `* 2.0 - 1.0` for both x and y; the Y flip was the
    // missing fix. X is unchanged because there is no X-flip flag.
    float2 uv = (float2(froxel_id.xy) + 0.5) / float2(push.froxel_width, push.froxel_height);
    float2 ndc_xy = float2((uv.x * 2.0) - 1.0, 1.0 - (uv.y * 2.0));
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

// PCG hash — same construction as mesh.slang for consistency. Fast, high-quality 32-bit
// hash for stochastic light sampling.
uint pcgHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float hashToFloat(uint hash)
{
    return float(hash) / 4294967296.0;
}

float luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Binary search the power-weighted CDF to select an emissive triangle index. Defensive
// early-out when count == 0; without it `hi = UINT_MAX` would loop ~32 times reading OOB.
uint searchCDF(float r, uint count)
{
    if (count == 0) {
        return 0;
    }
    uint lo = 0;
    uint hi = count - 1;
    while (lo < hi) {
        uint mid = (lo + hi) / 2;
        if (emissive_triangles[mid].cdf < r) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

// Henyey-Greenstein phase function. Models scattering anisotropy in a participating medium:
// g > 0 biases scattering forward (toward the light direction); g < 0 backward; g = 0
// reduces to isotropic (1 / 4π). Closed form is well-conditioned for all |g| < 1.
//
// phase(g, μ) = (1 - g²) / (4π · (1 + g² - 2g·μ)^1.5)   where μ = cos(θ)
//
// Reference: Henyey & Greenstein 1941; standard in real-time volumetric rendering
// (Frostbite, UE, Unity HDRP). For Tron-style atmospheric haze, g ≈ 0.6 gives visible
// forward-scattered light shafts when looking toward neon emitters through fog.
float henyeyGreenstein(float g, float cos_theta)
{
    float g2 = g * g;
    float denom_base = 1.0 + g2 - 2.0 * g * cos_theta;
    // Floor guards against denom_base ≈ 0 at extreme grazing angles with |g| ≈ 1; for
    // g = 0.6 this never triggers but keeps the function well-defined as g → 1.
    float denom = 4.0 * 3.14159265 * pow(max(denom_base, 1e-4), 1.5);
    return (1.0 - g2) / denom;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void injectMain(uint3 froxel_id : SV_DispatchThreadID)
{
    if ((froxel_id.x >= push.froxel_width) || (froxel_id.y >= push.froxel_height) || (froxel_id.z >= push.froxel_depth)) {
        return;
    }

    float3 world_pos = froxelToWorld(froxel_id);

    // ── Extinction (41a, unchanged) ──

    // Height-falloff density: full strength at and below fog_height_base, exponentially
    // attenuated above. Models a ground-hugging fog layer that's denser near the Tron
    // grid floor than up in the sky.
    float height_above = max(world_pos.y - push.fog_height_base, 0.0);
    float density = push.fog_density * exp(-height_above / push.fog_height_falloff);

    // Per-froxel slice thickness in world units → optical depth integrated over this
    // slice for the composite raymarch (which builds T_slice = exp(-optical_depth)).
    float slice_dz = froxelSliceThickness(froxel_id.z);
    float optical_depth = density * slice_dz;

    // ── In-scattered radiance (41b: emissive sampling) ──
    //
    // For an isolated point on the view ray at this froxel, the in-scattered radiance
    // toward the camera from a single area-light sample is:
    //
    //     L_scatter = σ_s · phase(view_dir, light_dir) · L_light · visibility · geometry
    //
    // where σ_s is the scattering coefficient (we model σ_a ≈ 0 for atmospheric haze, so
    // σ_s = σ_t = density), and the geometric term covers the area-light sampling Jacobian
    // (cos(light_normal, -L) · area / dist²) divided by the source PDF (power-weighted).
    //
    // The composite shader then accumulates `slice.rgb × T_accum` per Wronski 2014; we
    // store the per-slice in-scatter pre-multiplied by σ_s · slice_dz so the recurrence
    // in the composite stays simple (one MUL + one ADD per slice).

    float3 scattered = float3(0.0, 0.0, 0.0);

    if ((push.emissive_count > 0) && (density > 0.0)) {
        // ── Multi-sample area-light estimator ──
        //
        // Why not single-sample: with 17 000+ emissive triangles, P(pick any one
        // particular triangle) ~ 5e-5 and `1 / pdf_source` ~ 1/lum × total_power can run
        // 200+. So single-sample MC is essentially "0 most of the time, big spike
        // sometimes" — visually unacceptable noise without temporal reprojection.
        // FOG_SAMPLES per-froxel × variance reduction × N gets the sky from "static
        // snow" to "subtle glow"; 4 samples is the sweet spot before traversal cost
        // dominates. (41c temporal reprojection will let us drop this back to 1.)
        const uint FOG_SAMPLES = 4;
        // Per-sample firefly clamp — caps the spike from rare close-and-bright picks.
        // Tight value (3.0) is needed BEFORE averaging because `1/pdf × 1/dist²` can
        // produce values orders of magnitude above the ground-truth integral. Apply
        // before the average so a single huge sample can't dominate the mean.
        const float FOG_PER_SAMPLE_CLAMP = 3.0;

        // View direction — camera → froxel; cos(view, light) drives the phase function.
        float3 view_dir = normalize(world_pos - push.camera_pos);

        // Per-froxel base seed — re-mixed per sample inside the loop.
        uint base_seed = froxel_id.x * 1973u + froxel_id.y * 9277u + froxel_id.z * 5237u + push.frame_count * 26699u;

        float3 sample_sum = float3(0.0, 0.0, 0.0);

        for (uint sample_i = 0; sample_i < FOG_SAMPLES; ++sample_i) {
            uint seed = base_seed + sample_i * 7919u;
            uint hash0 = pcgHash(seed);
            uint hash1 = pcgHash(hash0);
            uint hash2 = pcgHash(hash1);

            float r0 = hashToFloat(hash0);
            float r1 = hashToFloat(hash1);
            float r2 = hashToFloat(hash2);

            // Power-weighted CDF lookup — same source PDF as the surface ReSTIR sampler.
            uint tri_idx = searchCDF(r0, push.emissive_count);
            EmissiveTriangle etri = emissive_triangles[tri_idx];

            // Uniform barycentric sample on the triangle (Turk 1990 / Shirley & Chiu).
            float su = sqrt(r1);
            float bary_u = 1.0 - su;
            float bary_v = su * r2;
            float bary_w = 1.0 - bary_u - bary_v;
            float3 light_point = bary_u * etri.v0 + bary_v * etri.v1 + bary_w * etri.v2;
            float3 light_normal = normalize(cross(etri.v1 - etri.v0, etri.v2 - etri.v0));

            // Geometry from froxel centre to the light sample.
            float3 to_light = light_point - world_pos;
            float dist_sq = max(dot(to_light, to_light), 1e-4);
            float dist = sqrt(dist_sq);
            float3 light_dir = to_light / dist;

            // Inverse-square fall-off + Lambert cosine on the light surface — the
            // area-light geometric kernel `cos(light_normal, -light_dir) / dist²`. The
            // triangle area does NOT appear here: the joint per-area PDF is `area_i ·
            // lum_i / total_power (cdf pick) × 1/area_i (uniform-on-triangle) = lum_i /
            // total_power` (m⁻²), so the area factor cancels exactly.
            float cos_light = max(dot(light_normal, -light_dir), 0.0);
            float pdf_source = max(luminance(etri.emissive) / push.total_emissive_power, 1e-8);
            float3 light_contribution = etri.emissive * (cos_light / dist_sq) / pdf_source;

            // Phase function modulates by view × light direction angle.
            float cos_theta = dot(view_dir, light_dir);
            float phase = henyeyGreenstein(push.hg_g, cos_theta);

            // Per-sample radiance estimate, pre-clamped to bound variance.
            float3 sample_radiance = optical_depth * phase * light_contribution;
            sample_radiance = min(sample_radiance, float3(FOG_PER_SAMPLE_CLAMP, FOG_PER_SAMPLE_CLAMP, FOG_PER_SAMPLE_CLAMP));

            // Visibility shadow ray. Three template flags (matches mesh.slang's pattern):
            //  - RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH — occlusion test, not closest-hit.
            //  - RAY_FLAG_SKIP_CLOSEST_HIT_SHADER — inline ray query has no shaders anyway.
            //  - RAY_FLAG_CULL_NON_OPAQUE — glass/pillar don't block (light passes through
            //    transparent surfaces).
            RayDesc ray;
            ray.Origin = world_pos;
            ray.Direction = light_dir;
            ray.TMin = 1e-3;
            ray.TMax = max(dist - 1e-3, 1e-3);

            RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_CULL_NON_OPAQUE> q;
            q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
            q.Proceed();
            bool occluded = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;

            if (!occluded) {
                sample_sum += sample_radiance;
            }
        }

        // MC mean across the FOG_SAMPLES samples.
        scattered = sample_sum / float(FOG_SAMPLES);
    }

    froxel_grid[froxel_id] = float4(scattered, optical_depth);
}
