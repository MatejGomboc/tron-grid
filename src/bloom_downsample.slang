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

// Bloom extraction and downsample compute shaders. Two entry points:
//   bloomExtractMain   — extracts bright pixels from HDR with Karis average
//   bloomDownsampleMain — 2×2 box-filter downsample for the mip chain

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> hdr_input;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> bloom_src;

[[vk::binding(2, 0)]]
[[vk::image_format("unknown")]]
RWTexture2D<float4> bloom_dst;

struct BloomPushConstants
{
    float threshold;
};

[[vk::push_constant]]
BloomPushConstants push;

// Rec. 709 luminance weights.
static const float3 LUMINANCE_WEIGHTS = float3(0.2126, 0.7152, 0.0722);

//! Computes luminance of a linear-light colour.
float luminance(float3 colour)
{
    return dot(colour, LUMINANCE_WEIGHTS);
}

//! Extracts bright pixels from the HDR image at half resolution with Karis average.
[shader("compute")]
[numthreads(8, 8, 1)]
void bloomExtractMain(uint3 thread_id : SV_DispatchThreadID)
{
    uint dst_w;
    uint dst_h;
    bloom_dst.GetDimensions(dst_w, dst_h);

    if (thread_id.x >= dst_w || thread_id.y >= dst_h)
    {
        return;
    }

    // Read a 2×2 block from the full-resolution HDR image.
    uint2 base = thread_id.xy * 2;
    float3 s00 = hdr_input[base + uint2(0, 0)].rgb;
    float3 s10 = hdr_input[base + uint2(1, 0)].rgb;
    float3 s01 = hdr_input[base + uint2(0, 1)].rgb;
    float3 s11 = hdr_input[base + uint2(1, 1)].rgb;

    // Threshold — zero out texels below the brightness threshold.
    s00 *= step(push.threshold, luminance(s00));
    s10 *= step(push.threshold, luminance(s10));
    s01 *= step(push.threshold, luminance(s01));
    s11 *= step(push.threshold, luminance(s11));

    // Karis average — weight each sample by 1/(1 + luminance) to suppress
    // fireflies from sub-pixel bright spots that would otherwise dominate
    // the downsample.
    float w00 = 1.0 / (1.0 + luminance(s00));
    float w10 = 1.0 / (1.0 + luminance(s10));
    float w01 = 1.0 / (1.0 + luminance(s01));
    float w11 = 1.0 / (1.0 + luminance(s11));
    float w_sum = w00 + w10 + w01 + w11;

    float3 result = (s00 * w00 + s10 * w10 + s01 * w01 + s11 * w11) / w_sum;

    bloom_dst[thread_id.xy] = float4(result, 1.0);
}

//! Downsamples the bloom source mip by a factor of 2 using a box filter.
[shader("compute")]
[numthreads(8, 8, 1)]
void bloomDownsampleMain(uint3 thread_id : SV_DispatchThreadID)
{
    uint dst_w;
    uint dst_h;
    bloom_dst.GetDimensions(dst_w, dst_h);

    if (thread_id.x >= dst_w || thread_id.y >= dst_h)
    {
        return;
    }

    // Read a 2×2 block from the source mip.
    uint2 base = thread_id.xy * 2;
    float3 s00 = bloom_src[base + uint2(0, 0)].rgb;
    float3 s10 = bloom_src[base + uint2(1, 0)].rgb;
    float3 s01 = bloom_src[base + uint2(0, 1)].rgb;
    float3 s11 = bloom_src[base + uint2(1, 1)].rgb;

    // Simple box filter — average of 4 texels.
    float3 result = (s00 + s10 + s01 + s11) * 0.25;

    bloom_dst[thread_id.xy] = float4(result, 1.0);
}
