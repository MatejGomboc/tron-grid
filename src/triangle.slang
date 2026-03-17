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

// Vertex input — matches C++ Vertex struct and VkVertexInputAttributeDescription layout.
struct VSInput {
    float3 position : POSITION;
    float4 colour : COLOR0;
};

// Vertex-to-fragment interpolated data.
struct VSOutput {
    float4 position : SV_Position;
    float4 colour : COLOR0;
};

[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.colour = input.colour;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target
{
    return input.colour;
}
