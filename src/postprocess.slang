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
// to the swapchain image. Currently performs identity tonemapping (clamp +
// sRGB gamma). Tonemapping and bloom will be added in Etapes 30-32.

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> hdr_input;

[[vk::binding(1, 0)]]
[[vk::image_format("unknown")]]
RWTexture2D<float4> swapchain_output;

// IEC 61966-2-1 sRGB transfer function constants.
static const float SRGB_LINEAR_THRESHOLD = 0.0031308;
static const float SRGB_LINEAR_SCALE = 12.92;
static const float SRGB_POWER_SCALE = 1.055;
static const float SRGB_POWER_EXPONENT = 1.0 / 2.4;
static const float SRGB_POWER_OFFSET = 0.055;

//! Converts a single linear-light channel to sRGB (exact IEC 61966-2-1 transfer function).
float linearToSrgb(float channel)
{
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

    if (thread_id.x >= width || thread_id.y >= height)
    {
        return;
    }

    float4 colour = hdr_input[thread_id.xy];

    // Clamp HDR values to displayable range.
    colour.rgb = saturate(colour.rgb);

    // Apply exact sRGB encoding (IEC 61966-2-1). The swapchain uses UNORM format
    // so the hardware does not apply gamma — the shader must encode manually.
    colour.r = linearToSrgb(colour.r);
    colour.g = linearToSrgb(colour.g);
    colour.b = linearToSrgb(colour.b);

    swapchain_output[thread_id.xy] = colour;
}
