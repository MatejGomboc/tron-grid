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

# Vulkan-specific definitions — scoped to this target only.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VK_NO_PROTOTYPES
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=0
    $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Linux>:VK_USE_PLATFORM_XCB_KHR>
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
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/triangle.spv
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
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/cull.spv
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
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/task.spv
    DEPENDS ${TASK_SOURCE}
    COMMENT "Compiling task.slang -> task.spv (with validation)"
    VERBATIM
)
# mesh.slang `import skybox_common;` — the shared procedural-sky module is resolved by
# slangc against the `-I <src dir>` search path (see Slang user guide
# "Compiling Code with Slang"). The DEPENDS list also tracks skybox_common.slang so
# changes to the module trigger a mesh.spv rebuild.
set(SKYBOX_COMMON_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/skybox_common.slang)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/mesh.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${MESH_SOURCE}
        -I ${CMAKE_CURRENT_SOURCE_DIR}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -fvk-invert-y
        -warnings-as-errors all
        -capability GL_EXT_ray_query
        -entry meshMain
        -entry fragMain
        -entry fragTransparent
        -o ${SHADER_OUTPUT_DIR}/mesh.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/mesh.spv
    DEPENDS ${MESH_SOURCE} ${SKYBOX_COMMON_SOURCE}
    COMMENT "Compiling mesh.slang -> mesh.spv (with validation)"
    VERBATIM
)

# Post-process shader (compute) — compile, then validate SPIR-V
set(POSTPROCESS_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/postprocess.slang)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/postprocess.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${POSTPROCESS_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -warnings-as-errors all
        -entry postprocessMain
        -o ${SHADER_OUTPUT_DIR}/postprocess.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/postprocess.spv
    DEPENDS ${POSTPROCESS_SOURCE}
    COMMENT "Compiling postprocess.slang -> postprocess.spv (with validation)"
    VERBATIM
)

# Bloom downsample shaders (two entry points from one source) — compile, then validate SPIR-V
set(BLOOM_DOWN_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/bloom_downsample.slang)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/bloom_extract.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${BLOOM_DOWN_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -warnings-as-errors all
        -entry bloomExtractMain
        -o ${SHADER_OUTPUT_DIR}/bloom_extract.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/bloom_extract.spv
    DEPENDS ${BLOOM_DOWN_SOURCE}
    COMMENT "Compiling bloom_downsample.slang (extract) -> bloom_extract.spv (with validation)"
    VERBATIM
)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/bloom_downsample.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${BLOOM_DOWN_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -warnings-as-errors all
        -entry bloomDownsampleMain
        -o ${SHADER_OUTPUT_DIR}/bloom_downsample.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/bloom_downsample.spv
    DEPENDS ${BLOOM_DOWN_SOURCE}
    COMMENT "Compiling bloom_downsample.slang (downsample) -> bloom_downsample.spv (with validation)"
    VERBATIM
)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/bloom_upsample.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${BLOOM_DOWN_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -warnings-as-errors all
        -entry bloomUpsampleMain
        -o ${SHADER_OUTPUT_DIR}/bloom_upsample.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/bloom_upsample.spv
    DEPENDS ${BLOOM_DOWN_SOURCE}
    COMMENT "Compiling bloom_downsample.slang (upsample) -> bloom_upsample.spv (with validation)"
    VERBATIM
)

# Volumetric fog shaders (Phase 8 Etape 41) — froxel density injection + raymarch composite.
# 41b adds RayQuery shadow rays to inject_density.slang, which auto-promotes the SPIR-V
# capability set; -capability GL_EXT_ray_query suppresses the warning that -warnings-as-errors
# would otherwise turn into a build failure (matches mesh.slang's compile flags).
set(INJECT_DENSITY_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/inject_density.slang)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/inject_density.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${INJECT_DENSITY_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -warnings-as-errors all
        -capability GL_EXT_ray_query
        -entry injectMain
        -o ${SHADER_OUTPUT_DIR}/inject_density.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/inject_density.spv
    DEPENDS ${INJECT_DENSITY_SOURCE}
    COMMENT "Compiling inject_density.slang -> inject_density.spv (with validation)"
    VERBATIM
)

set(VOLUMETRIC_FILTER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/volumetric_filter.slang)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/volumetric_filter.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${VOLUMETRIC_FILTER_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -warnings-as-errors all
        -entry filterMain
        -o ${SHADER_OUTPUT_DIR}/volumetric_filter.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/volumetric_filter.spv
    DEPENDS ${VOLUMETRIC_FILTER_SOURCE}
    COMMENT "Compiling volumetric_filter.slang -> volumetric_filter.spv (with validation)"
    VERBATIM
)

set(VOLUMETRIC_COMPOSITE_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/volumetric_composite.slang)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/volumetric_composite.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${VOLUMETRIC_COMPOSITE_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -warnings-as-errors all
        -entry compositeMain
        -o ${SHADER_OUTPUT_DIR}/volumetric_composite.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/volumetric_composite.spv
    DEPENDS ${VOLUMETRIC_COMPOSITE_SOURCE}
    COMMENT "Compiling volumetric_composite.slang -> volumetric_composite.spv (with validation)"
    VERBATIM
)

# SVGF denoiser shader (compute) — Phase 8 Etape 42c-polish-3 (Schied et al. 2017 SVGF for
# the single-bounce indirect GI channel). Single entry point with multi-pass parameterisation
# via push constants — host dispatches it 5 times per frame (1 init + 4 wavelet iterations
# at strides 1/2/4/8) per the canonical à-trous sequence (Dammertz et al. 2010).
set(SVGF_DENOISE_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/svgf_denoise.slang)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/svgf_denoise.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${SVGF_DENOISE_SOURCE}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -warnings-as-errors all
        -entry svgfMain
        -o ${SHADER_OUTPUT_DIR}/svgf_denoise.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/svgf_denoise.spv
    DEPENDS ${SVGF_DENOISE_SOURCE}
    COMMENT "Compiling svgf_denoise.slang -> svgf_denoise.spv (with validation)"
    VERBATIM
)

# Skybox shader (vertex + fragment) — compile, then validate SPIR-V.
# `import skybox_common;` is resolved by slangc against the `-I <src dir>` search path;
# DEPENDS tracks skybox_common.slang so module changes trigger a rebuild.
set(SKYBOX_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/skybox.slang)
add_custom_command(
    OUTPUT ${SHADER_OUTPUT_DIR}/skybox.spv
    COMMAND ${SLANGC_EXECUTABLE}
        ${SKYBOX_SOURCE}
        -I ${CMAKE_CURRENT_SOURCE_DIR}
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -fvk-invert-y
        -warnings-as-errors all
        -entry skyVertMain
        -entry skyFragMain
        -o ${SHADER_OUTPUT_DIR}/skybox.spv
    COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.3 ${SHADER_OUTPUT_DIR}/skybox.spv
    DEPENDS ${SKYBOX_SOURCE} ${SKYBOX_COMMON_SOURCE}
    COMMENT "Compiling skybox.slang -> skybox.spv (with validation)"
    VERBATIM
)

# Compile shaders — ALL target ensures they build even when the executable is up to date.
add_custom_target(shaders ALL DEPENDS
    ${SHADER_OUTPUT_DIR}/triangle.spv
    ${SHADER_OUTPUT_DIR}/cull.spv
    ${SHADER_OUTPUT_DIR}/task.spv
    ${SHADER_OUTPUT_DIR}/mesh.spv
    ${SHADER_OUTPUT_DIR}/postprocess.spv
    ${SHADER_OUTPUT_DIR}/bloom_extract.spv
    ${SHADER_OUTPUT_DIR}/bloom_downsample.spv
    ${SHADER_OUTPUT_DIR}/bloom_upsample.spv
    ${SHADER_OUTPUT_DIR}/inject_density.spv
    ${SHADER_OUTPUT_DIR}/volumetric_filter.spv
    ${SHADER_OUTPUT_DIR}/volumetric_composite.spv
    ${SHADER_OUTPUT_DIR}/svgf_denoise.spv
    ${SHADER_OUTPUT_DIR}/skybox.spv
)
add_dependencies(${PROJECT_NAME} shaders)

# Copy compiled shaders next to the executable.
# Uses an ALL custom target with explicit dependency on the shaders target
# so the copy always runs after shader compilation — unlike POST_BUILD
# which only triggers when the executable itself is relinked.
# copy_if_different makes this a no-op when files are unchanged.
add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/triangle.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/triangle.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/cull.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/cull.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/task.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/task.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/mesh.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/mesh.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/postprocess.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/postprocess.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/bloom_extract.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/bloom_extract.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/bloom_downsample.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/bloom_downsample.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/bloom_upsample.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/bloom_upsample.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/inject_density.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/inject_density.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/volumetric_filter.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/volumetric_filter.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/volumetric_composite.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/volumetric_composite.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/svgf_denoise.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/svgf_denoise.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SHADER_OUTPUT_DIR}/skybox.spv $<TARGET_FILE_DIR:${PROJECT_NAME}>/skybox.spv
    COMMENT "Copying shaders to output directory"
    VERBATIM
)
add_dependencies(copy_shaders shaders)
