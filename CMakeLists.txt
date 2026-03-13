cmake_minimum_required(VERSION 3.16)

project(TronGrid
    LANGUAGES CXX
    VERSION 0.1.0
    DESCRIPTION "Vulkan-based Tron Grid renderer"
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable exception handling for MSVC (suppresses C4530 warning)
if(MSVC)
    add_compile_options(/EHsc)
endif()

if(NOT (WIN32 OR (UNIX AND NOT APPLE)))
    message(FATAL_ERROR "Unsupported platform. Only Windows and Linux are supported.")
endif()

enable_testing()

add_subdirectory(libs)
add_subdirectory(src)
