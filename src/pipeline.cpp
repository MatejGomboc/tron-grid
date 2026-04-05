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
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <unistd.h>
#include <linux/limits.h>
#endif

std::string executableDirectory()
{
#ifdef _WIN32
    wchar_t wide_path[MAX_PATH]{};
    DWORD path_len{GetModuleFileNameW(nullptr, wide_path, MAX_PATH)};
    if (path_len == 0 || path_len >= MAX_PATH) {
        return {};
    }
    int len{WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, nullptr, 0, nullptr, nullptr)};
    if (len <= 0) {
        return {};
    }
    std::string narrow(static_cast<std::string::size_type>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, narrow.data(), len, nullptr, nullptr);
    std::string::size_type pos{narrow.find_last_of("\\/")};
    return (pos != std::string::npos) ? narrow.substr(0, pos + 1) : narrow;
#else
    char path[PATH_MAX]{};
    ssize_t count{readlink("/proc/self/exe", path, PATH_MAX)};
    std::string full(path, (count > 0) ? static_cast<std::string::size_type>(count) : 0);
    std::string::size_type pos{full.find_last_of('/')};
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

    std::streamsize file_size{file.tellg()};
    if ((file_size <= 0) || (file_size % sizeof(uint32_t) != 0)) {
        logger.logFatal("Invalid SPIR-V file size: " + path + ".");
        std::abort();
    }

    std::vector<uint32_t> buffer(static_cast<std::vector<uint32_t>::size_type>(file_size) / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    if (!file.good()) {
        logger.logFatal("Failed to read SPIR-V file: " + path + ".");
        std::abort();
    }

    logger.logInfo("Loaded SPIR-V: " + path + " (" + std::to_string(file_size) + " bytes).");
    return buffer;
}

Pipeline::Pipeline(const Device& device, vk::Format colour_format, vk::Format depth_format, const std::vector<uint32_t>& task_spirv,
    const std::vector<uint32_t>& mesh_frag_spirv, uint32_t frames_in_flight, LoggingLib::Logger& logger) :
    m_device(&device),
    m_logger(logger)
{
    // Shader modules — task shader and mesh+fragment combined module.
    vk::ShaderModuleCreateInfo task_module_info{};
    task_module_info.codeSize = task_spirv.size() * sizeof(uint32_t);
    task_module_info.pCode = task_spirv.data();
    vk::raii::ShaderModule task_module{device.get(), task_module_info};

    vk::ShaderModuleCreateInfo mesh_frag_module_info{};
    mesh_frag_module_info.codeSize = mesh_frag_spirv.size() * sizeof(uint32_t);
    mesh_frag_module_info.pCode = mesh_frag_spirv.data();
    vk::raii::ShaderModule mesh_frag_module{device.get(), mesh_frag_module_info};

    // Shader stages: task → mesh → fragment.
    std::array<vk::PipelineShaderStageCreateInfo, 3> shader_stages{};
    shader_stages[0].stage = vk::ShaderStageFlagBits::eTaskEXT;
    shader_stages[0].module = *task_module;
    shader_stages[0].pName = "taskMain";
    shader_stages[1].stage = vk::ShaderStageFlagBits::eMeshEXT;
    shader_stages[1].module = *mesh_frag_module;
    shader_stages[1].pName = "meshMain";
    shader_stages[2].stage = vk::ShaderStageFlagBits::eFragment;
    shader_stages[2].module = *mesh_frag_module; // Same module, different entry point.
    shader_stages[2].pName = "fragMain";

    // No vertex input — mesh shaders read from SSBOs.
    // No input assembly — mesh shaders output primitives directly.

    std::array<vk::DynamicState, 2> dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
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

    // Descriptor set layout — 8 bindings for the mesh shader pipeline.
    // Binding 0: camera UBO (task + mesh + fragment stages)
    // Binding 1: object transforms SSBO (task + mesh stages)
    // Binding 2: object bounds SSBO (task stage only)
    // Binding 3: meshlet descriptors SSBO (mesh stage only)
    // Binding 4: vertex data SSBO (mesh stage only)
    // Binding 5: meshlet vertex indices SSBO (mesh stage only)
    // Binding 6: meshlet triangle indices SSBO (mesh stage only)
    // Binding 7: TLAS (fragment stage — inline ray query for shadows)
    std::array<vk::DescriptorSetLayoutBinding, 8> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT | vk::ShaderStageFlagBits::eFragment;
    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = vk::ShaderStageFlagBits::eTaskEXT;
    bindings[3].binding = 3;
    bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = vk::ShaderStageFlagBits::eMeshEXT;
    bindings[4].binding = 4;
    bindings[4].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = vk::ShaderStageFlagBits::eMeshEXT;
    bindings[5].binding = 5;
    bindings[5].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = vk::ShaderStageFlagBits::eMeshEXT;
    bindings[6].binding = 6;
    bindings[6].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = vk::ShaderStageFlagBits::eMeshEXT;
    bindings[7].binding = 7;
    bindings[7].descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layout_info{};
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();
    m_descriptor_set_layout = vk::raii::DescriptorSetLayout{device.get(), layout_info};

    // Pipeline layout — 1 descriptor set + push constants for task shader.
    vk::PushConstantRange push_range{};
    push_range.stageFlags = vk::ShaderStageFlagBits::eTaskEXT;
    push_range.offset = 0;
    push_range.size = sizeof(TaskPushConstants);

    vk::PipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &*m_descriptor_set_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_range;
    m_layout = vk::raii::PipelineLayout{device.get(), pipeline_layout_info};

    // Dynamic rendering — same as before.
    vk::PipelineRenderingCreateInfo rendering_info{};
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &colour_format;
    rendering_info.depthAttachmentFormat = depth_format;

    // Mesh shader pipeline — no vertex input state, no input assembly state.
    vk::GraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.pNext = &rendering_info;
    pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = nullptr; // Mesh shaders don't use vertex input.
    pipeline_info.pInputAssemblyState = nullptr; // Mesh shaders output primitives directly.
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterisation;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &colour_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = *m_layout;

    m_pipeline = vk::raii::Pipeline{device.get(), nullptr, pipeline_info};

    // Descriptor pool — UBO + 6 SSBOs + 1 TLAS per frame.
    std::array<vk::DescriptorPoolSize, 3> pool_sizes{};
    pool_sizes[0].type = vk::DescriptorType::eUniformBuffer;
    pool_sizes[0].descriptorCount = frames_in_flight;
    pool_sizes[1].type = vk::DescriptorType::eStorageBuffer;
    pool_sizes[1].descriptorCount = frames_in_flight * 6;
    pool_sizes[2].type = vk::DescriptorType::eAccelerationStructureKHR;
    pool_sizes[2].descriptorCount = frames_in_flight;

    vk::DescriptorPoolCreateInfo pool_info{};
    pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pool_info.maxSets = frames_in_flight;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    m_descriptor_pool = vk::raii::DescriptorPool{device.get(), pool_info};

    std::vector<vk::DescriptorSetLayout> layouts(frames_in_flight, *m_descriptor_set_layout);
    vk::DescriptorSetAllocateInfo alloc_info{};
    alloc_info.descriptorPool = *m_descriptor_pool;
    alloc_info.descriptorSetCount = frames_in_flight;
    alloc_info.pSetLayouts = layouts.data();
    m_descriptor_sets = device.get().allocateDescriptorSets(alloc_info);

    m_ubo_mapped_ptrs.resize(frames_in_flight);
    for (uint32_t i{0}; i < frames_in_flight; ++i) {
        m_ubo_mapped_ptrs[i] = nullptr;
    }

    m_logger.logInfo("Mesh shader pipeline created (task + mesh + fragment).");
}

void Pipeline::bindUBO(uint32_t frame_index, VkBuffer buffer) const
{
    vk::DescriptorBufferInfo buffer_info{};
    buffer_info.buffer = buffer;
    buffer_info.offset = 0;
    buffer_info.range = sizeof(CameraUBO);

    vk::WriteDescriptorSet write{};
    write.dstSet = *m_descriptor_sets[frame_index];
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eUniformBuffer;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_info;

    m_device->get().updateDescriptorSets({write}, {});
}

void Pipeline::bindSSBOs(uint32_t frame_index, VkBuffer object_ssbo, VkDeviceSize object_size, VkBuffer bounds_ssbo, VkDeviceSize bounds_size, VkBuffer meshlet_desc_ssbo,
    VkDeviceSize meshlet_desc_size, VkBuffer vertex_ssbo, VkDeviceSize vertex_size, VkBuffer meshlet_vertex_indices_ssbo, VkDeviceSize meshlet_vertex_indices_size,
    VkBuffer meshlet_triangle_indices_ssbo, VkDeviceSize meshlet_triangle_indices_size) const
{
    std::array<vk::DescriptorBufferInfo, 6> buffer_infos{};
    buffer_infos[0].buffer = object_ssbo;
    buffer_infos[0].offset = 0;
    buffer_infos[0].range = object_size;
    buffer_infos[1].buffer = bounds_ssbo;
    buffer_infos[1].offset = 0;
    buffer_infos[1].range = bounds_size;
    buffer_infos[2].buffer = meshlet_desc_ssbo;
    buffer_infos[2].offset = 0;
    buffer_infos[2].range = meshlet_desc_size;
    buffer_infos[3].buffer = vertex_ssbo;
    buffer_infos[3].offset = 0;
    buffer_infos[3].range = vertex_size;
    buffer_infos[4].buffer = meshlet_vertex_indices_ssbo;
    buffer_infos[4].offset = 0;
    buffer_infos[4].range = meshlet_vertex_indices_size;
    buffer_infos[5].buffer = meshlet_triangle_indices_ssbo;
    buffer_infos[5].offset = 0;
    buffer_infos[5].range = meshlet_triangle_indices_size;

    std::array<vk::WriteDescriptorSet, 6> writes{};
    for (uint32_t i{0}; i < 6; ++i) {
        writes[i].dstSet = *m_descriptor_sets[frame_index];
        writes[i].dstBinding = i + 1; // Bindings 1-6 (0 is UBO).
        writes[i].dstArrayElement = 0;
        writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[i].descriptorCount = 1;
        writes[i].pBufferInfo = &buffer_infos[i];
    }

    m_device->get().updateDescriptorSets(writes, {});
}

void Pipeline::updateCameraUBO(uint32_t frame_index, const CameraUBO& ubo) const
{
    assert(frame_index < m_ubo_mapped_ptrs.size());
    assert(m_ubo_mapped_ptrs[frame_index] != nullptr);
    if (m_ubo_mapped_ptrs[frame_index]) {
        std::memcpy(m_ubo_mapped_ptrs[frame_index], &ubo, sizeof(CameraUBO));
    }
}

void Pipeline::bindTLAS(uint32_t frame_index, vk::AccelerationStructureKHR tlas) const
{
    vk::WriteDescriptorSetAccelerationStructureKHR as_write{};
    as_write.accelerationStructureCount = 1;
    as_write.pAccelerationStructures = &tlas;

    vk::WriteDescriptorSet write{};
    write.pNext = &as_write;
    write.dstSet = *m_descriptor_sets[frame_index];
    write.dstBinding = 7;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
    write.descriptorCount = 1;

    m_device->get().updateDescriptorSets({write}, {});
}
