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

#include "pipeline.hpp"
#include "device.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <linux/limits.h>
#endif

std::string executableDirectory()
{
#ifdef _WIN32
    wchar_t wide_path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, wide_path, MAX_PATH);
    int len = WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<std::string::size_type>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, narrow.data(), len, nullptr, nullptr);
    std::string::size_type pos = narrow.find_last_of("\\/");
    return (pos != std::string::npos) ? narrow.substr(0, pos + 1) : narrow;
#else
    char path[PATH_MAX]{};
    ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
    std::string full(path, (count > 0) ? static_cast<std::string::size_type>(count) : 0);
    std::string::size_type pos = full.find_last_of('/');
    return (pos != std::string::npos) ? full.substr(0, pos + 1) : full;
#endif
}

std::vector<uint32_t> loadSpirv(const std::string& path, LoggingLib::Logger& logger)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        logger.logFatal("Failed to open SPIR-V file: " + path + ".");
        std::abort();
    }

    std::streamsize file_size = static_cast<std::streamsize>(file.tellg());
    if (file_size <= 0 || file_size % sizeof(uint32_t) != 0) {
        logger.logFatal("Invalid SPIR-V file size: " + path + ".");
        std::abort();
    }

    std::vector<uint32_t> buffer(static_cast<std::vector<uint32_t>::size_type>(file_size) / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    file.close();

    logger.logInfo("Loaded SPIR-V: " + path + " (" + std::to_string(file_size) + " bytes).");
    return buffer;
}

Pipeline::Pipeline(const Device& device, vk::Format colour_format, const std::vector<uint32_t>& vert_spirv, const std::vector<uint32_t>& frag_spirv,
    LoggingLib::Logger& logger) :
    m_logger(logger)
{
    // Shader modules
    vk::ShaderModuleCreateInfo vert_module_info{};
    vert_module_info.codeSize = vert_spirv.size() * sizeof(uint32_t);
    vert_module_info.pCode = vert_spirv.data();
    vk::raii::ShaderModule vert_module(device.get(), vert_module_info);

    vk::ShaderModuleCreateInfo frag_module_info{};
    frag_module_info.codeSize = frag_spirv.size() * sizeof(uint32_t);
    frag_module_info.pCode = frag_spirv.data();
    vk::raii::ShaderModule frag_module(device.get(), frag_module_info);

    std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages{};
    shader_stages[0].stage = vk::ShaderStageFlagBits::eVertex;
    shader_stages[0].module = *vert_module;
    shader_stages[0].pName = "vertMain";
    shader_stages[1].stage = vk::ShaderStageFlagBits::eFragment;
    shader_stages[1].module = *frag_module;
    shader_stages[1].pName = "fragMain";

    // Vertex input — matches Vertex struct
    vk::VertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = vk::VertexInputRate::eVertex;

    std::array<vk::VertexInputAttributeDescription, 2> attributes{};
    // Location 0: float3 position
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = vk::Format::eR32G32B32Sfloat;
    attributes[0].offset = offsetof(Vertex, position);
    // Location 1: float4 colour
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
    attributes[1].offset = offsetof(Vertex, colour);

    vk::PipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    // Input assembly
    vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.topology = vk::PrimitiveTopology::eTriangleList;
    input_assembly.primitiveRestartEnable = vk::False;

    // Dynamic viewport + scissor
    std::array<vk::DynamicState, 2> dynamic_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    // Rasterisation
    vk::PipelineRasterizationStateCreateInfo rasterisation{};
    rasterisation.depthClampEnable = vk::False;
    rasterisation.rasterizerDiscardEnable = vk::False;
    rasterisation.polygonMode = vk::PolygonMode::eFill;
    rasterisation.cullMode = vk::CullModeFlagBits::eNone;
    rasterisation.frontFace = vk::FrontFace::eCounterClockwise;
    rasterisation.depthBiasEnable = vk::False;
    rasterisation.lineWidth = 1.0f;

    // Multisample — required even without MSAA
    vk::PipelineMultisampleStateCreateInfo multisample{};
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;
    multisample.sampleShadingEnable = vk::False;

    // Colour blend — no blending, write RGBA
    vk::PipelineColorBlendAttachmentState colour_blend_attachment{};
    colour_blend_attachment.blendEnable = vk::False;
    colour_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB
        | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo colour_blend{};
    colour_blend.logicOpEnable = vk::False;
    colour_blend.attachmentCount = 1;
    colour_blend.pAttachments = &colour_blend_attachment;

    // Pipeline layout — empty (no descriptors, no push constants)
    vk::PipelineLayoutCreateInfo layout_info{};
    m_layout = vk::raii::PipelineLayout(device.get(), layout_info);

    // Dynamic rendering — attach colour format
    vk::PipelineRenderingCreateInfo rendering_info{};
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &colour_format;

    // Graphics pipeline
    vk::GraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterisation;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &colour_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = *m_layout;

    m_pipeline = vk::raii::Pipeline(device.get(), nullptr, pipeline_info);

    m_logger.logInfo("Graphics pipeline created.");
}
