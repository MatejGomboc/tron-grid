cmake_minimum_required(VERSION 3.16)

project(TronGrid
    LANGUAGES CXX
    VERSION 0.1.0
    DESCRIPTION "Vulkan-based Tron Grid renderer"
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src)
