cmake_minimum_required(VERSION 3.16)

project(TronGrid
    LANGUAGES CXX
    VERSION 0.1.0
    DESCRIPTION "Vulkan-based Tron Grid renderer for Project MOTE"
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)

set(COMMON_SOURCES
    main.cpp
)

if(WIN32)
    set(PLATFORM_DEFINES VK_USE_PLATFORM_WIN32_KHR)
    set(PLATFORM_LIBS "")
    set(APP_TYPE "")

elseif(UNIX)
    find_package(X11 REQUIRED)
    set(PLATFORM_DEFINES VK_USE_PLATFORM_XLIB_KHR)
    set(PLATFORM_LIBS ${X11_LIBRARIES})
    set(APP_TYPE "")
endif()

add_executable(${PROJECT_NAME} ${APP_TYPE}
    ${COMMON_SOURCES}
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${Vulkan_INCLUDE_DIRS}"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    VK_NO_PROTOTYPES
    ${PLATFORM_DEFINES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${PLATFORM_LIBS}
    ${CMAKE_DL_LIBS}
)
