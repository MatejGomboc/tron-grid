# TODO & Development Journal

A shared task list and journal for humans and AI assistants working on TronGrid.

---

## Etape 1 — Internal Library Scaffolding ✅

Completed 2026-03-13. PR #18 merged.

- `libs/test_fixture/` — custom test framework with C++20 `std::source_location` + templates
- `libs/signal/` — header-only `signals::Signal<T>` thread-safe message queue
- `libs/window/` — platform windowing with hidden platform headers, `window::create()` factory
- All 3 libraries have CTest suites (3/3 pass)
- `CMakePresets.json` fully fleshed out with test + workflow presets

---

## Etape 2 — Vulkan Instance, Validation, and Device ✅

Completed 2026-03-13. PR #20 merged.

- Volk from Vulkan SDK for dynamic loading (`src/volk.cpp`)
- `gpu::Instance` with validation layers + debug messenger (debug builds)
- `gpu::Device` with GPU scoring (discrete preferred), graphics + present queues, dynamic rendering
- `gpu::create_surface` for platform surface (Win32/XCB) via vulkan-hpp
- Window library made Vulkan-agnostic (`nativeHandle()` / `nativeDisplay()`)
- C++20 best practices: `[[nodiscard]]`, `constexpr`, `std::ranges`, designated initialisers
- `//!` Qt-style doxygen comments across entire codebase

---

## Etape 3 — Swapchain, Command Buffers, and Clear Screen ✅

Completed 2026-03-15. PRs #22, #23, #24 merged.

- `Swapchain` class with MAILBOX present mode, B8G8R8A8_SRGB, old-swapchain reuse
- Double-buffered frame sync (fences + semaphores), command pool + per-frame command buffers
- Dynamic rendering clear to dark teal, Synchronization2 image layout barriers
- `SignalLib::Signal<ResizeEvent>` for window resize communication
- Bug fixes: dangling pointer, orphaned semaphore, 0x0 extent, `VK_QUEUE_FAMILY_IGNORED`,
  Vulkan 1.3 device check, Win32 spurious `WM_SIZE`, `PeekMessageW` `WM_QUIT`, XCB fixes
- Full naming convention sweep: camelCase functions, `m_` member prefix, `WindowLib`/`SignalLib`/`TestFixtureLib` namespaces
- Comprehensive doxygen coverage, full GPL v3 licence headers, STYLE.md enforced

---

## Current Etape: 4 — Triangle on Screen (Phase 0 Finale)

**Branch:** create from `main`, name `feat/triangle`

**Goal:** Integrate VMA for GPU memory allocation, compile Slang shaders to SPIR-V, create a
graphics pipeline with dynamic rendering, and draw a hardcoded colourful triangle. After this
etape, a red-green-blue triangle is visible on the dark teal background — **Phase 0 complete**.

**Read before starting:** `docs/ARCHITECTURE.md` § Rendering Pipeline, `CLAUDE.md` § Key Design
Decisions (coordinate system, colour space, descriptor model, Slang shaders)

### Dependencies to integrate

| Library | Purpose | Integration |
|---------|---------|-------------|
| VMA | GPU memory allocation for buffers | `FetchContent` from GitHub (GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) |
| Slang | Shader compilation to SPIR-V | `slangc` from Vulkan SDK, CMake custom commands |

### Steps (do in order)

#### 1. Integrate VMA

- Add `FetchContent` for VMA in root `CMakeLists.txt` (after `find_package(Vulkan)`)
- Link `VulkanMemoryAllocator` in `src/CMakeLists.txt`
- **Volk compatibility:** VMA assumes static Vulkan linking by default. Add
  `VMA_STATIC_VULKAN_FUNCTIONS=0` and `VMA_DYNAMIC_VULKAN_FUNCTIONS=0` to the global
  compile definitions in root `CMakeLists.txt`. Then use the official one-liner
  `vmaImportVulkanFunctionsFromVolk()` to populate `VmaVulkanFunctions` — no manual
  function pointer filling needed (requires `volk.h` included before `vk_mem_alloc.h`)
- **Include order matters:** `volk.h` → `vulkan/vulkan_raii.hpp` → `vk_mem_alloc.h`
- Create `src/allocator.hpp` / `src/allocator.cpp` — thin RAII wrapper around `VmaAllocator`
    - Constructor: call `vmaImportVulkanFunctionsFromVolk()` then `vmaCreateAllocator()`
    - Destructor: `vmaDestroyAllocator()`
    - Provide `createBuffer()` that returns a RAII buffer+allocation pair (VMA returns raw
      `VkBuffer`, not `vk::raii::Buffer` — the wrapper must call `vmaDestroyBuffer` on cleanup)
- **Destruction order:** in `main()`, the allocator must be declared after the device but before
  any buffers, so RAII destruction frees buffers → allocator → device (reverse order)
- Verify: project builds with VMA linked but not yet used in the render loop

#### 2. Write Slang shaders

Create `src/triangle.vert.slang` and `src/triangle.frag.slang` alongside the C++ sources.

**Vertex shader:**

- Input: `float3 position` (location 0) + `float4 colour` (location 1)
- Output: `float4 sv_position` + `float4 colour` (to fragment)
- No transforms — pass position through as `float4(position, 1.0)`

**Fragment shader:**

- Input: interpolated `float4 colour` from vertex stage
- Output: `float4` colour to the colour attachment

#### 3. Compile shaders via CMake

Find `slangc` from the Vulkan SDK — there is no `FindVulkan` component for Slang, so use:

```cmake
find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
```

Add a custom CMake target (`shaders`) in `src/CMakeLists.txt`:

- Use `add_custom_command` for each shader with flags:
    - `-target spirv -profile spirv_1_4 -emit-spirv-directly`
    - `-fvk-use-entrypoint-name` (preserve entry point names)
    - `-fvk-invert-y` (Vulkan Y-axis flip — avoids negating Y in shader source)
    - `-entry main -stage vertex` / `-stage fragment`
- Output `.spv` files to `${CMAKE_CURRENT_BINARY_DIR}`
- Use `add_custom_target(shaders DEPENDS triangle.vert.spv triangle.frag.spv)` to group them
- `add_dependencies(${PROJECT_NAME} shaders)` so the executable depends on compiled shaders
- Add a `POST_BUILD` command to copy `.spv` files next to the executable (`$<TARGET_FILE_DIR:${PROJECT_NAME}>`) so they can be loaded at runtime via a relative path
- This ensures shaders rebuild when `.slang` sources change (via `DEPENDS`)

Add a helper function in `src/` to load `.spv` files from disc into `std::vector<uint32_t>`,
then create `vk::raii::ShaderModule` from the loaded SPIR-V bytes.

**Runtime path resolution:** load `.spv` files from the same directory as the executable.
Use `GetModuleFileNameW` on Win32 or `/proc/self/exe` readlink on Linux to find the
executable path, strip the filename, and append the `.spv` filename. This avoids depending
on the working directory.

#### 4. Create the graphics pipeline

Create `src/pipeline.hpp` / `src/pipeline.cpp`.

Pipeline configuration:

- **Shader stages:** vertex + fragment from the loaded shader modules
- **Vertex input:** one binding (stride = `sizeof(Vertex)`), two attributes:
    - Location 0: `R32G32B32_SFLOAT` at offset 0 (position)
    - Location 1: `R32G32B32A32_SFLOAT` at offset 12 (colour)
- **Input assembly:** `eTriangleList`
- **Viewport + scissor:** set per-frame from swapchain extent (not baked into pipeline)
- **Dynamic state:** `VkPipelineDynamicStateCreateInfo` with `VK_DYNAMIC_STATE_VIEWPORT` +
  `VK_DYNAMIC_STATE_SCISSOR` — this is required so the viewport/scissor can change each frame
  without recreating the pipeline
- **Rasterisation:** fill mode, no culling (both faces visible), counter-clockwise front
- **Multisample:** `VkPipelineMultisampleStateCreateInfo` with `rasterizationSamples = 1`
  (required even without MSAA — the pipeline won't create without it)
- **Colour blend:** no blending, write RGBA
- **Dynamic rendering:** chain `VkPipelineRenderingCreateInfo` with the swapchain colour format
- **Pipeline layout:** empty (no descriptors, no push constants yet)
- Use `vk::raii::Pipeline` and `vk::raii::PipelineLayout`

#### 5. Allocate the triangle vertex buffer

Define the vertex data:

```cpp
struct Vertex {
    float position[3];
    float colour[4];
};

constexpr std::array<Vertex, 3> TRIANGLE_VERTICES = {{
    {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},  // Red   (bottom-left)
    {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},  // Green (bottom-right)
    {{ 0.0f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},  // Blue  (top)
}};
```

Allocation strategy (use `VMA_MEMORY_USAGE_AUTO` — the old `GPU_ONLY`/`CPU_ONLY` are deprecated):

1. Create a staging buffer with `VMA_MEMORY_USAGE_AUTO` +
   `VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT`
   (auto-mapped, no manual `vkMapMemory` needed) — copy vertex data via `memcpy` into `allocInfo.pMappedData`
2. Create the GPU vertex buffer with `VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE` +
   `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT`
3. Record a one-shot command buffer to copy staging → GPU buffer
4. Submit, wait, free the staging buffer

#### 6. Update the render loop

Between `cmd.beginRendering()` and `cmd.endRendering()` in `recordClearCommand`
(rename to `recordFrame` or similar):

```text
1. Bind the graphics pipeline
2. Set viewport and scissor (from swapchain extent)
3. Bind the vertex buffer
4. Draw 3 vertices, 1 instance
```

The existing clear colour (dark teal) stays as the `loadOp = eClear` background.

#### 7. Handle pipeline recreation on swapchain resize

The pipeline uses dynamic viewport/scissor state, so **no pipeline recreation is needed**
on resize — just update the viewport and scissor values each frame.

#### 8. Update `src/CMakeLists.txt`

- Add `allocator.cpp`, `pipeline.cpp` to source list
- Add shader compilation custom commands
- Link `VulkanMemoryAllocator`

#### 9. Run clang-format, build, test, commit, and PR

- `clang-format -i` on all changed C++ files
- Build all 5 presets (or at least windows-msvc + linux-x11-gcc)
- `ctest` — all existing tests still pass
- Manual verification: colourful triangle on dark teal background, resize works, ESC closes
- Branch: `feat/triangle`
- PR against `main`

### Acceptance Criteria

- [ ] VMA integrated via FetchContent, RAII allocator wrapper
- [ ] Slang shaders compiled to SPIR-V via `slangc` at build time
- [ ] Graphics pipeline created with `VkPipelineRenderingCreateInfo` (no VkRenderPass)
- [ ] Triangle vertex buffer allocated via VMA (staging → GPU copy)
- [ ] Colourful triangle visible on screen (red, green, blue corners)
- [ ] Dark teal background still visible around the triangle
- [ ] Dynamic viewport/scissor — no pipeline recreation on resize
- [ ] `vk::raii` for all new Vulkan objects — no manual destroy
- [ ] Proper doxygen on all new classes, methods, and members
- [ ] Code follows `.clang-format` and `STYLE.md`
- [ ] British spelling in all comments and documentation
- [ ] All existing tests still pass
- [ ] **Phase 0 complete — triangle on screen**

---

## Backlog

<!-- Ideas, improvements, and tasks for later phases. -->

---

## Journal

<!-- Reverse chronological — newest entries at the top. -->
<!-- Format: ### YYYY-MM-DD — Short title -->

### 2026-03-13 — Architecture crystallised

Documented the full engine architecture across VISION, ARCHITECTURE, and AI_INTERFACE docs:

- Single-player engine, one AI brain (DLL/SO) per instance
- Console-first startup: human mode creates window, bot mode stays console
- All core subsystems (rendering, physics, audio, sensory) written in-house
- Internal static libraries under `libs/` (LEGO bricks, plain namespaces, submodule-ready)
- Modern Vulkan patterns: vk::raii, dynamic rendering, component model, Signal<T>, rendergraph
- AI bot interface via shared memory, staged sensory protocol, standalone C header
- Future goals: MMO multiplayer (Phase 10), VR mode

### 2026-03-08 — Project scaffolding complete

Adopted full GitHub infrastructure from git-proxy-mcp:

- Issue and PR templates
- Split CI workflows (ci_main + ci_pr) with markdownlint
- Release workflow (CMake-based, Windows + Linux artefacts)
- Cache cleanup workflow + script
- CONTRIBUTING, CODE_OF_CONDUCT, SECURITY, STYLE, CHANGELOG
- VS Code workspace config (settings, extensions, debug for all 5 presets)
- docs/ folder (ARCHITECTURE.md, VISION.md)
- Moved sources into src/ directory

Layered CMake setup: root CMakeLists.txt delegates to src/CMakeLists.txt (ready for future libs/ subdirectory).

The repo is now ready for Phase 0 implementation.
