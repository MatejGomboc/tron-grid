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
    math
    ${CMAKE_DL_LIBS}
)

# Slang shader compilation — slangc from Vulkan SDK
find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
find_program(SPIRV_VAL_EXECUTABLE spirv-val HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

# Triangle shader (vertex + fragment) — compile, then validate SPIR-V
set(TRIANGLE_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/triangle.slang)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/triangle.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${TRIANGLE_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -fvk-invert-y
        -warnings-as-errors all
        -entry vertMain
        -entry fragMain
        -o ${SHADER_OUTPUT_DIR}/triangle.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} ${SHADER_OUTPUT_DIR}/triangle.spv
    DEPENDS ${TRIANGLE_SOURCE}
    COMMENT "Compiling triangle.slang -> triangle.spv (with validation)"
    VERBATIM
)

# Cull shader (compute) — compile, then validate SPIR-V
set(CULL_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/cull.slang)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/cull.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${CULL_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -warnings-as-errors all
        -entry cullMain
        -o ${SHADER_OUTPUT_DIR}/cull.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} ${SHADER_OUTPUT_DIR}/cull.spv
    DEPENDS ${CULL_SOURCE}
    COMMENT "Compiling cull.slang -> cull.spv (with validation)"
    VERBATIM
)

add_custom_target(shaders DEPENDS
    ${SHADER_OUTPUT_DIR}/triangle.spv
    ${SHADER_OUTPUT_DIR}/cull.spv
)
add_dependencies(${PROJECT_NAME} shaders)

# Copy compiled shaders next to the executable
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/triangle.spv
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/triangle.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/cull.spv
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/cull.spv
    COMMENT "Copying shaders to output directory"
    VERBATIM
)
