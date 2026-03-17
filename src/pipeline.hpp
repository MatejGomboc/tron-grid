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

#pragma once

#include <volk/volk.h>
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>
#include <log/logger.hpp>
#include <cstdint>
#include <string>
#include <vector>

class Device; // forward declaration

//! Loads a SPIR-V binary from disc and returns it as a uint32_t vector.
[[nodiscard]] std::vector<uint32_t> loadSpirv(const std::string& path, LoggingLib::Logger& logger);

//! Returns the directory containing the running executable (with trailing separator).
[[nodiscard]] std::string executableDirectory();

//! Vertex layout for the triangle — position (float3) + colour (float4).
struct Vertex {
    float position[3]; //!< Clip-space position.
    float colour[4]; //!< RGBA colour.
};

/*!
    Owns the graphics pipeline and its layout.
    Uses dynamic rendering (no VkRenderPass).
*/
class Pipeline {
public:
    /*!
        Creates the graphics pipeline for the triangle.

        \param device The logical device.
        \param colour_format The swapchain colour attachment format.
        \param vert_spirv Vertex shader SPIR-V binary.
        \param frag_spirv Fragment shader SPIR-V binary.
        \param logger Logger reference.
    */
    Pipeline(const Device& device, vk::Format colour_format, const std::vector<uint32_t>& vert_spirv, const std::vector<uint32_t>& frag_spirv,
        LoggingLib::Logger& logger);

    // Non-copyable, movable
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = default;
    Pipeline& operator=(Pipeline&&) = default;

    //! RAII pipeline handle.
    [[nodiscard]] const vk::raii::Pipeline& get() const
    {
        return m_pipeline;
    }

    //! Pipeline layout handle.
    [[nodiscard]] const vk::raii::PipelineLayout& layout() const
    {
        return m_layout;
    }

private:
    LoggingLib::Logger& m_logger; //!< Logger reference (non-owning).
    vk::raii::PipelineLayout m_layout{nullptr}; //!< Pipeline layout (empty — no descriptors yet).
    vk::raii::Pipeline m_pipeline{nullptr}; //!< Graphics pipeline handle.
};
