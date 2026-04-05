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

// Post-process compute shader — reads from the HDR framebuffer and writes
// to the swapchain image. Applies the ACES fitted RRT+ODT (Stephen Hill /
// MJP BakingLab) for hue-preserving tonemapping, followed by exact sRGB
// gamma encoding. Bloom will be added in Etapes 31-32.

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> hdr_input;

[[vk::binding(1, 0)]]
[[vk::image_format("unknown")]]
RWTexture2D<float4> swapchain_output;

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

    float4 colour = hdr_input[thread_id.xy];

    // ACES fitted RRT+ODT — transforms to AP1 colour space, applies the
    // tone curve, transforms back. The AP1 matrices reduce hue shifts that
    // per-channel tonemapping in sRGB space causes (orange → yellow).
    colour.rgb = acesTonemap(colour.rgb);

    // Apply exact sRGB encoding (IEC 61966-2-1). The swapchain uses UNORM format
    // so the hardware does not apply gamma — the shader must encode manually.
    colour.r = linearToSrgb(colour.r);
    colour.g = linearToSrgb(colour.g);
    colour.b = linearToSrgb(colour.b);
    colour.a = 1.0;

    swapchain_output[thread_id.xy] = colour;
}
