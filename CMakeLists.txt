cmake_minimum_required(VERSION 3.25)

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

enable_testing()

add_subdirectory(libs)
add_subdirectory(src)
