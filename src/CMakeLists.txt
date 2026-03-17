add_executable(${PROJECT_NAME}
    main.cpp
    volk.cpp
    vma.cpp
    instance.cpp
    device.cpp
    surface.cpp
    swapchain.cpp
    allocator.cpp
    pipeline.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    window
    signals
    logging
    ${CMAKE_DL_LIBS}
)

# Slang shader compilation — slangc from Vulkan SDK
find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

set(SHADER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/triangle.slang)
set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/triangle.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${SHADER_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -entry vertMain
        -entry fragMain
        -o ${SHADER_OUTPUT_DIR}/triangle.spv
    DEPENDS ${SHADER_SOURCE}
    COMMENT "Compiling triangle.slang -> triangle.spv"
    VERBATIM
)

add_custom_target(shaders DEPENDS ${SHADER_OUTPUT_DIR}/triangle.spv)
add_dependencies(${PROJECT_NAME} shaders)

# Copy compiled shader next to the executable
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/triangle.spv
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/triangle.spv
    COMMENT "Copying triangle.spv to output directory"
    VERBATIM
)
