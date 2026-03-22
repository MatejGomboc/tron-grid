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

// Camera uniform buffer — matches C++ CameraUBO struct.
struct CameraData {
    float4x4 view;
    float4x4 projection;
};

// Per-object data — matches C++ ObjectData struct.
struct ObjectData {
    float4x4 model;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraData> camera;

[[vk::binding(1, 0)]]
StructuredBuffer<ObjectData> objects;

// Compacted visible object indices — written by the compute culling shader.
[[vk::binding(2, 0)]]
StructuredBuffer<uint> visible_indices;

// Vertex input — matches C++ Vertex struct (position + normal + UV).
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

// Vertex-to-fragment interpolated data.
struct VSOutput {
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertMain(VSInput input, uint instance_id : SV_InstanceID)
{
    // Map the compacted instance index back to the original object index
    uint object_index = visible_indices[instance_id];
    ObjectData obj = objects[object_index];

    VSOutput output;
    float4 world_pos = mul(obj.model, float4(input.position, 1.0));
    output.position = mul(camera.projection, mul(camera.view, world_pos));
    // Transform normal by model matrix (sufficient for uniform scale — no inverse transpose needed yet).
    output.normal = mul((float3x3)obj.model, input.normal);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target
{
    // Visualise normals as colour until we have proper lighting.
    float3 colour = input.normal * 0.5 + 0.5;
    return float4(colour, 1.0);
}
