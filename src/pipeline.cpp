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
#include "allocator.hpp"
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

Pipeline::Pipeline(const Device& device, vk::Format colour_format, vk::Format depth_format, const std::vector<uint32_t>& vert_spirv,
    const std::vector<uint32_t>& frag_spirv, const std::vector<uint32_t>& cull_spirv, uint32_t frames_in_flight, LoggingLib::Logger& logger) :
    m_device(&device),
    m_logger(logger)
{
    // ── Graphics pipeline ──

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

    // Vertex input — matches Vertex struct (position + normal + UV)
    vk::VertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = vk::VertexInputRate::eVertex;

    std::array<vk::VertexInputAttributeDescription, 3> attributes{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = vk::Format::eR32G32B32Sfloat;
    attributes[0].offset = offsetof(Vertex, position);
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = vk::Format::eR32G32B32Sfloat;
    attributes[1].offset = offsetof(Vertex, normal);
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = vk::Format::eR32G32Sfloat;
    attributes[2].offset = offsetof(Vertex, uv);

    vk::PipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.topology = vk::PrimitiveTopology::eTriangleList;
    input_assembly.primitiveRestartEnable = vk::False;

    std::array<vk::DynamicState, 2> dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterisation{};
    rasterisation.depthClampEnable = vk::False;
    rasterisation.rasterizerDiscardEnable = vk::False;
    rasterisation.polygonMode = vk::PolygonMode::eFill;
    rasterisation.cullMode = vk::CullModeFlagBits::eNone;
    rasterisation.frontFace = vk::FrontFace::eCounterClockwise;
    rasterisation.depthBiasEnable = vk::False;
    rasterisation.lineWidth = 1.0f;

    vk::PipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.depthTestEnable = vk::True;
    depth_stencil.depthWriteEnable = vk::True;
    depth_stencil.depthCompareOp = vk::CompareOp::eLess;
    depth_stencil.depthBoundsTestEnable = vk::False;
    depth_stencil.stencilTestEnable = vk::False;

    vk::PipelineMultisampleStateCreateInfo multisample{};
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;
    multisample.sampleShadingEnable = vk::False;

    vk::PipelineColorBlendAttachmentState colour_blend_attachment{};
    colour_blend_attachment.blendEnable = vk::False;
    colour_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB
        | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo colour_blend{};
    colour_blend.logicOpEnable = vk::False;
    colour_blend.attachmentCount = 1;
    colour_blend.pAttachments = &colour_blend_attachment;

    // Graphics descriptor set layout — binding 0: UBO, 1: object SSBO, 2: visible indices
    std::array<vk::DescriptorSetLayoutBinding, 3> gfx_bindings{};
    gfx_bindings[0].binding = 0;
    gfx_bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    gfx_bindings[0].descriptorCount = 1;
    gfx_bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;
    gfx_bindings[1].binding = 1;
    gfx_bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
    gfx_bindings[1].descriptorCount = 1;
    gfx_bindings[1].stageFlags = vk::ShaderStageFlagBits::eVertex;
    gfx_bindings[2].binding = 2;
    gfx_bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
    gfx_bindings[2].descriptorCount = 1;
    gfx_bindings[2].stageFlags = vk::ShaderStageFlagBits::eVertex;

    vk::DescriptorSetLayoutCreateInfo gfx_layout_info{};
    gfx_layout_info.bindingCount = static_cast<uint32_t>(gfx_bindings.size());
    gfx_layout_info.pBindings = gfx_bindings.data();
    m_graphics_descriptor_set_layout = vk::raii::DescriptorSetLayout(device.get(), gfx_layout_info);

    vk::PipelineLayoutCreateInfo gfx_pipeline_layout_info{};
    gfx_pipeline_layout_info.setLayoutCount = 1;
    gfx_pipeline_layout_info.pSetLayouts = &*m_graphics_descriptor_set_layout;
    m_graphics_layout = vk::raii::PipelineLayout(device.get(), gfx_pipeline_layout_info);

    vk::PipelineRenderingCreateInfo rendering_info{};
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &colour_format;
    rendering_info.depthAttachmentFormat = depth_format;

    vk::GraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterisation;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &colour_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = *m_graphics_layout;

    m_graphics_pipeline = vk::raii::Pipeline(device.get(), nullptr, pipeline_info);

    // Graphics descriptor pool — UBO + 2 SSBOs per frame
    std::array<vk::DescriptorPoolSize, 2> gfx_pool_sizes{};
    gfx_pool_sizes[0].type = vk::DescriptorType::eUniformBuffer;
    gfx_pool_sizes[0].descriptorCount = frames_in_flight;
    gfx_pool_sizes[1].type = vk::DescriptorType::eStorageBuffer;
    gfx_pool_sizes[1].descriptorCount = frames_in_flight * 2; // object SSBO + visible indices

    vk::DescriptorPoolCreateInfo gfx_pool_info{};
    gfx_pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    gfx_pool_info.maxSets = frames_in_flight;
    gfx_pool_info.poolSizeCount = static_cast<uint32_t>(gfx_pool_sizes.size());
    gfx_pool_info.pPoolSizes = gfx_pool_sizes.data();
    m_graphics_descriptor_pool = vk::raii::DescriptorPool(device.get(), gfx_pool_info);

    std::vector<vk::DescriptorSetLayout> gfx_layouts(frames_in_flight, *m_graphics_descriptor_set_layout);
    vk::DescriptorSetAllocateInfo gfx_alloc_info{};
    gfx_alloc_info.descriptorPool = *m_graphics_descriptor_pool;
    gfx_alloc_info.descriptorSetCount = frames_in_flight;
    gfx_alloc_info.pSetLayouts = gfx_layouts.data();
    m_graphics_descriptor_sets = device.get().allocateDescriptorSets(gfx_alloc_info);

    m_ubo_mapped_ptrs.resize(frames_in_flight);
    for (uint32_t i = 0; i < frames_in_flight; ++i) {
        m_ubo_mapped_ptrs[i] = nullptr;
    }

    m_logger.logInfo("Graphics pipeline created.");

    // ── Compute culling pipeline ──

    vk::ShaderModuleCreateInfo cull_module_info{};
    cull_module_info.codeSize = cull_spirv.size() * sizeof(uint32_t);
    cull_module_info.pCode = cull_spirv.data();
    vk::raii::ShaderModule cull_module(device.get(), cull_module_info);

    // Compute descriptor set layout — 4 SSBOs
    std::array<vk::DescriptorSetLayoutBinding, 4> compute_bindings{};
    // Binding 0: object bounds (read-only)
    compute_bindings[0].binding = 0;
    compute_bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
    compute_bindings[0].descriptorCount = 1;
    compute_bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
    // Binding 1: indirect draw commands (read-write)
    compute_bindings[1].binding = 1;
    compute_bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
    compute_bindings[1].descriptorCount = 1;
    compute_bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;
    // Binding 2: draw count (read-write)
    compute_bindings[2].binding = 2;
    compute_bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
    compute_bindings[2].descriptorCount = 1;
    compute_bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;
    // Binding 3: visible indices (read-write)
    compute_bindings[3].binding = 3;
    compute_bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
    compute_bindings[3].descriptorCount = 1;
    compute_bindings[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo compute_layout_info{};
    compute_layout_info.bindingCount = static_cast<uint32_t>(compute_bindings.size());
    compute_layout_info.pBindings = compute_bindings.data();
    m_compute_descriptor_set_layout = vk::raii::DescriptorSetLayout(device.get(), compute_layout_info);

    // Compute pipeline layout — 1 descriptor set + push constants for frustum planes
    vk::PushConstantRange cull_push_range{};
    cull_push_range.stageFlags = vk::ShaderStageFlagBits::eCompute;
    cull_push_range.offset = 0;
    cull_push_range.size = sizeof(CullPushConstants);

    vk::PipelineLayoutCreateInfo compute_pipeline_layout_info{};
    compute_pipeline_layout_info.setLayoutCount = 1;
    compute_pipeline_layout_info.pSetLayouts = &*m_compute_descriptor_set_layout;
    compute_pipeline_layout_info.pushConstantRangeCount = 1;
    compute_pipeline_layout_info.pPushConstantRanges = &cull_push_range;
    m_compute_layout = vk::raii::PipelineLayout(device.get(), compute_pipeline_layout_info);

    vk::PipelineShaderStageCreateInfo cull_stage{};
    cull_stage.stage = vk::ShaderStageFlagBits::eCompute;
    cull_stage.module = *cull_module;
    cull_stage.pName = "cullMain";

    vk::ComputePipelineCreateInfo compute_create_info{};
    compute_create_info.stage = cull_stage;
    compute_create_info.layout = *m_compute_layout;

    m_compute_pipeline = vk::raii::Pipeline(device.get(), nullptr, compute_create_info);

    // Compute descriptor pool — 4 SSBOs × 1 set
    vk::DescriptorPoolSize compute_pool_size{};
    compute_pool_size.type = vk::DescriptorType::eStorageBuffer;
    compute_pool_size.descriptorCount = 4;

    vk::DescriptorPoolCreateInfo compute_pool_info{};
    compute_pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    compute_pool_info.maxSets = 1;
    compute_pool_info.poolSizeCount = 1;
    compute_pool_info.pPoolSizes = &compute_pool_size;
    m_compute_descriptor_pool = vk::raii::DescriptorPool(device.get(), compute_pool_info);

    vk::DescriptorSetAllocateInfo compute_alloc_info{};
    compute_alloc_info.descriptorPool = *m_compute_descriptor_pool;
    compute_alloc_info.descriptorSetCount = 1;
    compute_alloc_info.pSetLayouts = &*m_compute_descriptor_set_layout;
    m_compute_descriptor_sets = device.get().allocateDescriptorSets(compute_alloc_info);

    m_logger.logInfo("Compute culling pipeline created.");
}

void Pipeline::bindUBO(uint32_t frame_index, VkBuffer buffer) const
{
    vk::DescriptorBufferInfo buffer_info{};
    buffer_info.buffer = buffer;
    buffer_info.offset = 0;
    buffer_info.range = sizeof(CameraUBO);

    vk::WriteDescriptorSet write{};
    write.dstSet = *m_graphics_descriptor_sets[frame_index];
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eUniformBuffer;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_info;

    m_device->get().updateDescriptorSets({write}, {});
}

void Pipeline::bindObjectSSBO(uint32_t frame_index, VkBuffer buffer, VkDeviceSize size) const
{
    vk::DescriptorBufferInfo buffer_info{};
    buffer_info.buffer = buffer;
    buffer_info.offset = 0;
    buffer_info.range = size;

    vk::WriteDescriptorSet write{};
    write.dstSet = *m_graphics_descriptor_sets[frame_index];
    write.dstBinding = 1;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eStorageBuffer;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_info;

    m_device->get().updateDescriptorSets({write}, {});
}

void Pipeline::bindVisibleIndices(uint32_t frame_index, VkBuffer buffer, VkDeviceSize size) const
{
    vk::DescriptorBufferInfo buffer_info{};
    buffer_info.buffer = buffer;
    buffer_info.offset = 0;
    buffer_info.range = size;

    vk::WriteDescriptorSet write{};
    write.dstSet = *m_graphics_descriptor_sets[frame_index];
    write.dstBinding = 2;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eStorageBuffer;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_info;

    m_device->get().updateDescriptorSets({write}, {});
}

void Pipeline::bindComputeResources(VkBuffer bounds_buffer, VkDeviceSize bounds_size, VkBuffer indirect_buffer, VkDeviceSize indirect_size, VkBuffer draw_count_buffer,
    VkDeviceSize draw_count_size, VkBuffer visible_indices_buffer, VkDeviceSize visible_indices_size) const
{
    std::array<vk::DescriptorBufferInfo, 4> buffer_infos{};
    buffer_infos[0].buffer = bounds_buffer;
    buffer_infos[0].offset = 0;
    buffer_infos[0].range = bounds_size;
    buffer_infos[1].buffer = indirect_buffer;
    buffer_infos[1].offset = 0;
    buffer_infos[1].range = indirect_size;
    buffer_infos[2].buffer = draw_count_buffer;
    buffer_infos[2].offset = 0;
    buffer_infos[2].range = draw_count_size;
    buffer_infos[3].buffer = visible_indices_buffer;
    buffer_infos[3].offset = 0;
    buffer_infos[3].range = visible_indices_size;

    std::array<vk::WriteDescriptorSet, 4> writes{};
    for (uint32_t i = 0; i < 4; ++i) {
        writes[i].dstSet = *m_compute_descriptor_sets[0];
        writes[i].dstBinding = i;
        writes[i].dstArrayElement = 0;
        writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[i].descriptorCount = 1;
        writes[i].pBufferInfo = &buffer_infos[i];
    }

    m_device->get().updateDescriptorSets(writes, {});
}

void Pipeline::updateCameraUBO(uint32_t frame_index, const CameraUBO& ubo) const
{
    if (m_ubo_mapped_ptrs[frame_index]) {
        std::memcpy(m_ubo_mapped_ptrs[frame_index], &ubo, sizeof(CameraUBO));
    }
}
