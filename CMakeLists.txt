cmake_minimum_required(VERSION 3.16)

project(TronGrid
    LANGUAGES CXX
    VERSION 0.1.0
    DESCRIPTION "Vulkan-based Tron Grid renderer"
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings — high level, warnings as errors, all compilers
if(MSVC)
    add_compile_options(/EHsc /W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if(NOT (WIN32 OR (UNIX AND NOT APPLE)))
    message(FATAL_ERROR "Unsupported platform. Only Windows and Linux are supported.")
endif()

# Vulkan SDK — provides Volk, vulkan-hpp, and validation layers
find_package(Vulkan REQUIRED)

# Never link Vulkan statically — Volk handles dynamic loading
add_compile_definitions(VK_NO_PROTOTYPES)
add_compile_definitions(VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)

# VMA uses Volk for function pointers — disable its built-in static/dynamic loading
add_compile_definitions(VMA_STATIC_VULKAN_FUNCTIONS=0)
add_compile_definitions(VMA_DYNAMIC_VULKAN_FUNCTIONS=0)

# Platform surface extensions for Vulkan
if(WIN32)
    add_compile_definitions(VK_USE_PLATFORM_WIN32_KHR)
    add_compile_definitions(NOMINMAX)       # prevent Windows.h min/max macros
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
else()
    add_compile_definitions(VK_USE_PLATFORM_XCB_KHR)
endif()

enable_testing()

add_subdirectory(libs)
add_subdirectory(src)
