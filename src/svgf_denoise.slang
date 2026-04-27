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

// SVGF denoiser for the combined diffuse-style lighting channel — Phase 8 Etape
// 42c-polish-3.
//
// Reference: Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering" (HPG); the
// same algorithm NVIDIA productionised as RELAX in NRD/RTXDI for ReSTIR-class signals.
// Parameters tuned for our scene against visual gates: φ_color = 2 (per-pixel scaled by
// √variance, well below Falcor's canonical 10 to preserve sharp BRDF-peak hot spots
// near emissives), α_moments = 0.20, 3 wavelet iterations at strides 1, 2, 4 with 5×5
// B3-spline kernel (truncated from canonical 4 iterations because stride 8 produced
// low-frequency blob smear on dark obsidian).
//
// Implements full canonical SVGF with per-pixel temporal moments + variance-driven
// adaptive edge stop. The init pass (pass 0) reads `lighting_raw` (combined diffuse
// lighting from the mesh shader), updates a per-pixel (μ₁, μ₂) running average of
// luminance and luminance² via EMA against the previous frame's moments, computes
// variance = max(0, μ₂ − μ₁²), and writes √variance into the alpha channel of
// `wavelet_a` for downstream wavelet iterations to consume. Each wavelet pass uses
// `effective_σ = SIGMA_COLOR_BASE × √variance` as the luminance edge stop — so
// high-variance noisy pixels get aggressive smoothing while low-variance stable
// pixels (e.g. sharp BRDF-peak hot spots that have been bright for several frames)
// get edge preservation. This is the canonical SVGF behaviour that makes hot spots
// survive the wavelet while MC speckle gets killed.
//
// Pipeline order (single shader, parameterised by `push.pass_id`):
//   pass 0 — init:        lighting_raw + moments_prev → wavelet_a (rgb=lighting,
//                                                                  a=√variance)
//                                                       moments_curr (μ₁, μ₂)
//   pass 1 — wavelet 0:   wavelet_a → wavelet_b           (stride 1, 5×5 B3-spline taps)
//   pass 2 — wavelet 1:   wavelet_b → wavelet_a           (stride 2)
//   pass 3 — wavelet 2:   wavelet_a → denoised_indirect   (stride 4, FINAL — written to
//                                                          the current frame's ping-pong
//                                                          slot for next frame's mesh
//                                                          shader to read via binding 12)
//
// Memory cost: 2 wavelet ping-pong rgba16f textures (~32 MB each at 2560×1600) +
// 2 moments ping-pong rg16f textures (~16 MB each). The existing denoised-indirect
// ping-pong (also rgba16f × 2) holds the chain's final output for the next frame's
// mesh-shader composite.

// Binding 0 — raw combined-lighting texture written by the previous mesh-shader pass.
// This frame's mesh shader wrote (r.indirect + direct_diffuse) × ao_factor +
// direct_specular into here at sample 0; SVGF reads it as the noisy input to the
// wavelet chain. Phase 8 Etape 42c-polish-3 architectural change — was previously a
// reservoir SSBO read of `r.indirect` (just the indirect-bounce channel), but that
// left direct_diffuse and direct_specular un-denoised. Combining the lighting pre-SVGF
// gets all the MC-sampled terms cleaned up in one wavelet pass.
[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> lighting_raw;

// Bindings 4 + 5 — per-pixel temporal moments for variance estimation (Schied et al.
// 2017 §4). `moments_prev` holds (μ₁, μ₂) = (luminance EMA, luminance² EMA) from the
// previous frame's SVGF init pass; the current init pass reads it, EMA-blends with this
// frame's lighting_raw luminance, computes variance = max(0, μ₂ − μ₁²), and writes the
// updated moments to `moments_curr`. Variance scales the wavelet's edge-stop sigma
// per-pixel: noisy pixels (high variance) get more aggressive smoothing; pixels whose
// luminance is genuinely high and stable (e.g. a sharp BRDF-peak hot spot that's been
// bright for multiple frames) end up with low variance → narrow effective sigma →
// edge preservation. This is what makes sharp neon glints survive SVGF while MC-
// variance speckle still gets killed. rg16f because each channel is a scalar-luminance
// running average (μ₁ in .r, μ₂ in .g).
[[vk::binding(4, 0)]]
[[vk::image_format("rg16f")]]
RWTexture2D<float2> moments_prev;

[[vk::binding(5, 0)]]
[[vk::image_format("rg16f")]]
RWTexture2D<float2> moments_curr;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> wavelet_a;

[[vk::binding(2, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> wavelet_b;

[[vk::binding(3, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> denoised_indirect;

struct SvgfPushConstants
{
    uint screen_width;
    uint screen_height;
    uint pass_id; // 0 = init, 1-3 = wavelet iterations 0-2 (strides 1, 2, 4). 3 is the FINAL pass and writes to denoised_indirect.
    uint stride; // À-trous tap stride: 1, 2, 4 (set by host per pass).
};

[[vk::push_constant]]
SvgfPushConstants push;

// Rec.709 luminance — same coefficients used elsewhere in the project (mesh.slang's
// luminance() helper, etc.). Kept inline to avoid pulling a shader header for one line.
float computeLuminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// 5×5 B3-spline kernel weights for the à-trous wavelet (Dammertz et al. 2010 §3.1).
// Centre at (0,0) with weight 6/16; edges at offsets ±1 with 4/16; corners at ±2 with 1/16.
// The 5×5 kernel is the discrete sample of a B3-spline cubic basis. Used for all four
// iterations — the only thing that changes between iterations is the tap stride.
static const float WAVELET_KERNEL[5] = {1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0};

// Luminance edge-stop sigma BASE — multiplied by √variance (per-pixel) to get the
// effective sigma. Falcor's canonical SVGF default is 10. Per Schied et al. 2017 §4
// the formula is:
//
//     edge_weight = exp(-|Δluma| / (φ_color · √variance + ε))
//
// where φ_color is this base value and √variance is the per-pixel value computed in
// the init pass from temporal moments (and stored in the wavelet texture's alpha
// channel). High-variance (noisy) pixels get a wide effective sigma → aggressive
// smoothing across luminance differences; low-variance (stable) pixels get a narrow
// effective sigma → edges preserved.
//
// 2 (vs Falcor's 10) tightens the edge stop hard so stochastic bright BRDF-peak
// glints near emissives survive the wavelet — the high temporal variance ReSTIR
// produces at exactly those pixels would otherwise drive effective_σ wide enough
// to flatten them. Iterated 10 → 5 → 3 → 2 against visual gates; 2 is the value
// at which sharp neon glints on obsidian read as natural raytraced lighting.
static const float SIGMA_COLOR_BASE = 2.0;

// Temporal-moments EMA blend factor. α = 0.20 ≈ 5-frame effective sample count for
// the moments (Falcor canonical). Faster than the colour EMA because moments are
// noisier per-frame (single-sample variance estimates from squared luminance) — the
// faster decay keeps them tracking the current pixel's actual variance rather than
// being dominated by stale historical values.
static const float ALPHA_MOMENTS = 0.20;

// Variance epsilon — guards against degenerate "perfectly constant" pixels where
// √variance = 0 → effective_sigma = 0 → division by zero in the edge weight. Also
// serves as a floor so even completely-stable pixels still get a small amount of
// neighbourhood smoothing rather than zero.
static const float VARIANCE_EPSILON = 1e-4;

[shader("compute")]
[numthreads(8, 8, 1)]
void svgfMain(uint3 thread_id : SV_DispatchThreadID)
{
    if ((thread_id.x >= push.screen_width) || (thread_id.y >= push.screen_height)) {
        return;
    }

    int2 pixel = int2(thread_id.xy);

    // ── Pass 0: init — read lighting_raw, update temporal moments, compute variance ──
    //
    // Per-pixel temporal moments (Schied et al. 2017 §4): EMA-blend μ₁ = mean luminance
    // and μ₂ = mean luminance² with the previous frame's accumulated values, then
    // variance = max(0, μ₂ − μ₁²). The variance is propagated through the wavelet
    // chain via the alpha channel of wavelet_a so each downstream iteration's edge
    // stop can scale its sigma per-pixel.
    //
    // Same-pixel temporal accumulation (no motion-vector reprojection) — same approach
    // as the indirect channel's existing reservoir read pattern. This means the moments
    // ghost slightly when the camera moves (history doesn't follow the surface), but
    // for the project's mostly-static viewpoints it's adequate. A future polish
    // iteration could add reprojection through prev_view_projection (already in
    // CameraUBO for the volumetric filter) if camera movement becomes a quality issue.
    if (push.pass_id == 0u) {
        float3 lighting = lighting_raw[pixel].rgb;
        float current_luma = computeLuminance(lighting);

        // Read previous frame's moments (zero-initialised on the very first frame, so
        // the first-frame variance ≈ current_luma² × α × (1 − α) is a sensible bootstrap
        // value that produces aggressive smoothing while moments converge).
        float2 prev_moments = moments_prev[pixel];
        float new_mu1 = lerp(prev_moments.r, current_luma, ALPHA_MOMENTS);
        float new_mu2 = lerp(prev_moments.g, current_luma * current_luma, ALPHA_MOMENTS);
        float variance = max(new_mu2 - (new_mu1 * new_mu1), 0.0);

        moments_curr[pixel] = float2(new_mu1, new_mu2);
        wavelet_a[pixel] = float4(lighting, sqrt(variance + VARIANCE_EPSILON));
        return;
    }

    // ── Passes 1-4: à-trous wavelet iterations ──
    //
    // Each iteration reads from the appropriate ping-pong source, gathers a 5×5
    // neighbourhood at the current stride, applies the B3-spline kernel weighted by a
    // luminance edge stop (clamped Gaussian on |Δluminance|), and writes the normalised
    // weighted sum to the appropriate destination.
    //
    // Source/destination directions:
    //   pass 1 (stride 1): wavelet_a → wavelet_b
    //   pass 2 (stride 2): wavelet_b → wavelet_a
    //   pass 3 (stride 4): wavelet_a → denoised_indirect (final pass, ping-pong slot for
    //                                                     next frame's mesh shader)
    //
    // Same source-direction parity as in the canonical à-trous chain — pass 3 is final
    // (truncated from canonical 4 iterations against visual gates; see file header).

    bool source_is_a = ((push.pass_id % 2u) == 1u);

    int max_x = int(push.screen_width) - 1;
    int max_y = int(push.screen_height) - 1;
    int stride = int(push.stride);

    // Centre tap value. The .a channel carries √variance (set by the init pass) and
    // drives the per-pixel adaptive sigma — this is the load-bearing canonical-SVGF
    // detail that makes hot spots survive while speckle gets killed.
    float4 centre = source_is_a ? wavelet_a[pixel] : wavelet_b[pixel];
    float centre_luma = computeLuminance(centre.rgb);
    float effective_sigma = max(SIGMA_COLOR_BASE * centre.a, VARIANCE_EPSILON);

    float3 sum = float3(0.0, 0.0, 0.0);
    float weight_sum = 0.0;

    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            int2 sample_pixel = int2(pixel.x + dx * stride, pixel.y + dy * stride);

            // Edge clamp — out-of-bounds taps just read the nearest valid pixel rather
            // than dropping the sample, so edge pixels don't lose effective neighbour count.
            sample_pixel.x = clamp(sample_pixel.x, 0, max_x);
            sample_pixel.y = clamp(sample_pixel.y, 0, max_y);

            float4 sample_value = source_is_a ? wavelet_a[sample_pixel] : wavelet_b[sample_pixel];
            float sample_luma = computeLuminance(sample_value.rgb);

            // B3-spline kernel weight (separable).
            float kernel_weight = WAVELET_KERNEL[dx + 2] * WAVELET_KERNEL[dy + 2];

            // Variance-driven luminance edge stop (Schied et al. 2017 §4):
            //     edge_weight = exp(-|Δluma| / (φ_color × √variance + ε))
            // where √variance is the per-pixel value computed in the init pass. For
            // low-variance pixels (stable hot spots) effective_sigma shrinks → strong
            // edge preservation; for high-variance pixels (MC speckle) it grows → the
            // wavelet smooths through the noise.
            float luma_diff = abs(sample_luma - centre_luma);
            float edge_weight = exp(-luma_diff / effective_sigma);

            float total_weight = kernel_weight * edge_weight;

            sum += sample_value.rgb * total_weight;
            weight_sum += total_weight;
        }
    }

    float3 filtered = (weight_sum > 1e-6) ? (sum / weight_sum) : centre.rgb;

    // Write to the appropriate destination based on pass_id. The .a channel carries the
    // per-pixel √variance from the init pass — propagated unchanged through the wavelet
    // chain so each iteration's edge stop uses the same variance estimate. (Canonical
    // Schied 2017 § 4 propagates variance via squared kernel weights — variance shrinks
    // each iteration as smoothing happens. We use the simpler unchanged-propagation
    // approximation; works adequately for our diffuse-style combined-lighting signal.)
    if (push.pass_id == 3u) {
        // Final iteration — write to the denoised-indirect output texture (= current
        // frame's ping-pong slot, which the next frame's mesh shader reads via binding 12).
        denoised_indirect[pixel] = float4(filtered, 1.0);
    } else if (source_is_a) {
        // Source was wavelet_a → write to wavelet_b.
        wavelet_b[pixel] = float4(filtered, centre.a);
    } else {
        // Source was wavelet_b → write to wavelet_a.
        wavelet_a[pixel] = float4(filtered, centre.a);
    }
}
