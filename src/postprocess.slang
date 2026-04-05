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

//! Converts a single linear-light channel to sRGB (exact IEC 61966-2-1 transfer function).
float linearToSrgb(float linear)
{
    if (linear <= 0.0031308)
    {
        return linear * 12.92;
    }
    return 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
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
