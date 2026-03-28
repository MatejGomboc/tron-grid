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
    meshlet.cpp
    terrain.cpp
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

# Mesh shader pipeline (task + mesh + fragment) — compile, then validate SPIR-V
set(TASK_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/task.slang)
set(MESH_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/mesh.slang)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/task.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${TASK_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -warnings-as-errors all
        -entry taskMain
        -o ${SHADER_OUTPUT_DIR}/task.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} ${SHADER_OUTPUT_DIR}/task.spv
    DEPENDS ${TASK_SOURCE}
    COMMENT "Compiling task.slang -> task.spv (with validation)"
    VERBATIM
)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/mesh.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${MESH_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -fvk-invert-y
        -warnings-as-errors all
        -capability GL_EXT_ray_query
        -entry meshMain
        -entry fragMain
        -o ${SHADER_OUTPUT_DIR}/mesh.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} ${SHADER_OUTPUT_DIR}/mesh.spv
    DEPENDS ${MESH_SOURCE}
    COMMENT "Compiling mesh.slang -> mesh.spv (with validation)"
    VERBATIM
)

add_custom_target(shaders ALL DEPENDS
    ${SHADER_OUTPUT_DIR}/triangle.spv
    ${SHADER_OUTPUT_DIR}/cull.spv
    ${SHADER_OUTPUT_DIR}/task.spv
    ${SHADER_OUTPUT_DIR}/mesh.spv
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
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/task.spv
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/task.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/mesh.spv
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/mesh.spv
    COMMENT "Copying shaders to output directory"
    VERBATIM
)
