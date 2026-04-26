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

// Post-process compute shader — reads from the HDR framebuffer, composites
// bloom, applies cinematic effects, and writes the tonemapped result to the
// swapchain image. Pipeline: subtle chromatic aberration (HDR) → ACES fitted
// RRT+ODT tonemapping → sRGB encoding → cool colour grade → vignette.
//
// Phase 8 Etape 42c-0 modernisation: dropped the CRT scan-line overlay
// (3-pixel-period horizontal stripes — the most overtly retro element) and
// halved chromatic aberration. Modern Tron (Legacy 2010 / Ares 2025) doesn't
// use scan lines; the cleaner geometric look reads more contemporary. Bonus:
// removes a per-pixel high-frequency alternating brightness pattern that
// would actively fight TAA history clamping in the upcoming 42c-ii work.

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> hdr_input;

[[vk::binding(1, 0)]]
[[vk::image_format("unknown")]]
RWTexture2D<float4> swapchain_output;

[[vk::binding(2, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> bloom_texture;

struct PostProcessPushConstants
{
    float bloom_strength;
};

[[vk::push_constant]]
PostProcessPushConstants push;

// ACES colour space matrices (Stephen Hill / MJP BakingLab).
// sRGB primaries → approximate AP1 (Academy Color Encoding System).
static const float3x3 ACES_INPUT_MAT = float3x3(
    0.59719, 0.35458, 0.04823,
    0.07600, 0.90834, 0.01566,
    0.02840, 0.13383, 0.83777
);

// Approximate AP1 → sRGB primaries.
static const float3x3 ACES_OUTPUT_MAT = float3x3(
     1.60475, -0.53108, -0.07367,
    -0.10208,  1.10813, -0.00605,
    -0.00327, -0.07276,  1.07602
);

// IEC 61966-2-1 sRGB transfer function constants.
static const float SRGB_LINEAR_THRESHOLD = 0.0031308;
static const float SRGB_LINEAR_SCALE = 12.92;
static const float SRGB_POWER_SCALE = 1.055;
static const float SRGB_POWER_EXPONENT = 1.0 / 2.4;
static const float SRGB_POWER_OFFSET = 0.055;

// Cinematic post-process effect constants.
static const float CA_STRENGTH = 0.0015; // Chromatic aberration — RGB fringe intensity at screen edges (halved in Etape 42c-0 for a more modern look).
static const float COLOUR_GRADE_STRENGTH = 0.15; // Cool colour grade — lerp toward desaturated cool tint.
static const float VIGNETTE_STRENGTH = 1.5; // Vignette — radial darkening at screen corners.

// Rec. 709 luminance weights (for colour grading desaturation).
static const float3 LUMINANCE_WEIGHTS = float3(0.2126, 0.7152, 0.0722);

//! Fitted RRT+ODT curve applied per-channel in AP1 space (Hill / MJP).
float3 rrtAndOdtFit(float3 v)
{
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

//! ACES fitted tonemapping with AP1 colour space transforms (Hill / MJP BakingLab).
float3 acesTonemap(float3 colour)
{
    colour = mul(ACES_INPUT_MAT, colour);
    colour = rrtAndOdtFit(colour);
    colour = mul(ACES_OUTPUT_MAT, colour);
    return saturate(colour);
}

//! Converts a single linear-light channel to sRGB (exact IEC 61966-2-1 transfer function).
float linearToSrgb(float channel)
{
    channel = max(channel, 0.0);
    if (channel <= SRGB_LINEAR_THRESHOLD)
    {
        return channel * SRGB_LINEAR_SCALE;
    }
    return SRGB_POWER_SCALE * pow(channel, SRGB_POWER_EXPONENT) - SRGB_POWER_OFFSET;
}

//! Reads the HDR image with bloom composite at the given integer coordinate (clamped to valid bounds).
float3 sampleHdrWithBloom(uint2 coord, uint width, uint height, uint bloom_w, uint bloom_h)
{
    uint2 clamped = min(coord, uint2(width - 1, height - 1));
    float3 hdr = hdr_input[clamped].rgb;
    uint2 bloom_coord = min(clamped / 2, uint2(bloom_w - 1, bloom_h - 1));
    return hdr + push.bloom_strength * bloom_texture[bloom_coord].rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void postprocessMain(uint3 thread_id : SV_DispatchThreadID)
{
    uint width;
    uint height;
    hdr_input.GetDimensions(width, height);

    if ((thread_id.x >= width) || (thread_id.y >= height))
    {
        return;
    }

    uint bloom_w;
    uint bloom_h;
    bloom_texture.GetDimensions(bloom_w, bloom_h);

    // ── Chromatic aberration (HDR space) ──
    // Per-channel UV offset that increases toward screen edges — subtle RGB fringe.
    // Offsets must be clamped to [0, extent-1] as signed int *before* casting to uint —
    // otherwise negative offsets near the top-left corner wrap to huge uint values and
    // the internal min() in sampleHdrWithBloom ends up sampling the bottom-right of
    // the frame, producing a bright red fringe at the top-left edge.
    float2 uv = (float2(thread_id.xy) + 0.5) / float2(width, height);
    float2 centre_offset = uv - 0.5;
    float dist_sq = dot(centre_offset, centre_offset);
    float2 aberration = centre_offset * dist_sq * CA_STRENGTH;
    int2 pixel_offset = int2(aberration * float2(width, height));

    int2 centre_coord = int2(thread_id.xy);
    int2 max_coord = int2(int(width) - 1, int(height) - 1);
    int2 coord_r = clamp(centre_coord + pixel_offset, int2(0, 0), max_coord);
    int2 coord_b = clamp(centre_coord - pixel_offset, int2(0, 0), max_coord);

    float3 colour;
    colour.r = sampleHdrWithBloom(uint2(coord_r), width, height, bloom_w, bloom_h).r;
    colour.g = sampleHdrWithBloom(thread_id.xy, width, height, bloom_w, bloom_h).g;
    colour.b = sampleHdrWithBloom(uint2(coord_b), width, height, bloom_w, bloom_h).b;

    // ── ACES fitted RRT+ODT ──
    // Transforms to AP1 colour space, applies the tone curve, transforms back.
    colour = acesTonemap(colour);

    // ── sRGB encoding (IEC 61966-2-1) ──
    // The swapchain uses UNORM format so the hardware does not apply gamma.
    colour.r = linearToSrgb(colour.r);
    colour.g = linearToSrgb(colour.g);
    colour.b = linearToSrgb(colour.b);

    // ── Cool colour grade ──
    // Lerp toward a desaturated cool-tinted version — suppresses warm colours,
    // shifts toward the cyan-blue palette of Tron Legacy.
    float luminance = dot(colour, LUMINANCE_WEIGHTS);
    float3 cool_tint = float3(luminance * 0.9, luminance * 0.95, luminance * 1.1);
    colour = lerp(colour, cool_tint, COLOUR_GRADE_STRENGTH);

    // ── Vignette ──
    // Radial falloff darkens screen corners, draws the eye inward. Subtle and
    // photographic — kept after the Etape 42c-0 modernisation pass since it reads as
    // timeless cinematic, not retro CRT.
    float vignette = 1.0 - dot(centre_offset, centre_offset) * VIGNETTE_STRENGTH;
    vignette = saturate(vignette);
    colour *= vignette;

    swapchain_output[thread_id.xy] = float4(colour, 1.0);
}
