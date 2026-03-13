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
- Window library made Vulkan-agnostic (`native_handle()` / `native_display()`)
- C++20 best practices: `[[nodiscard]]`, `constexpr`, `std::ranges`, designated initialisers
- `//!` Qt-style doxygen comments across entire codebase

---

## Current Etape: 3 — Swapchain, Command Buffers, and Clear Screen

**Branch:** create from `main`, name `feat/swapchain`

**Goal:** Create a swapchain with MAILBOX present mode, set up command buffers and frame
synchronisation, and run a basic render loop that clears the screen to a colour. After this
etape, the window shows a solid colour (not black) and handles resize correctly.

**Note:** VMA (Vulkan Memory Allocator) is **deferred to Etape 4** — swapchain images are
managed by the presentation engine and require no manual memory allocation.

**Read before starting:** `docs/ARCHITECTURE.md` § Frame Synchronisation, § Dynamic Rendering,
and § Key Design Decisions (MAILBOX present mode, sRGB output, 16-bit float HDR)

### Steps (do in order)

#### 1. `gpu::Swapchain` — swapchain + image views

Create `src/gpu_swapchain.hpp` and `src/gpu_swapchain.cpp`.

```cpp
namespace gpu {
class Swapchain {
    vk::raii::SwapchainKHR swapchain_;
    std::vector<vk::Image> images_;           // non-owning (managed by swapchain)
    std::vector<vk::raii::ImageView> views_;  // RAII image views
    vk::SurfaceFormatKHR format_;
    vk::Extent2D extent_;
    vk::PresentModeKHR present_mode_;
};
}
```

Init sequence:

1. Query surface capabilities via `physical_device.getSurfaceCapabilitiesKHR(surface)`
2. Choose surface format:
   - Prefer `B8G8R8A8_SRGB` with `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`
   - Fall back to the first available format
3. Choose present mode:
   - Prefer `MAILBOX` (low latency, no tearing)
   - Fall back to `FIFO` (always available, guaranteed by spec)
4. Choose extent:
   - Use `currentExtent` from surface capabilities if not `0xFFFFFFFF`
   - Otherwise use the window dimensions, clamped to `minImageExtent` / `maxImageExtent`
5. Choose image count:
   - `minImageCount + 1` (for triple buffering headroom)
   - Clamp to `maxImageCount` if non-zero
6. Create `vk::raii::SwapchainKHR`:
   - `imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`
   - If graphics and present families differ: `imageSharingMode = CONCURRENT`
   - Otherwise: `imageSharingMode = EXCLUSIVE`
   - `preTransform = currentTransform`
   - `compositeAlpha = OPAQUE`
   - `clipped = true`
7. Retrieve swapchain images via `swapchain.getImages()`
8. Create one `vk::raii::ImageView` per image:
   - `viewType = 2D`, `format = chosen surface format`
   - `subresourceRange = { COLOUR, mipLevel 0, 1 level, arrayLayer 0, 1 layer }`

Provide a `recreate(uint32_t width, uint32_t height)` method that destroys and rebuilds
the swapchain + image views (reusing the old swapchain handle for smoother transitions).

#### 2. Frame synchronisation objects

Add frame-in-flight management to `src/main.cpp` or a small `gpu::FrameSync` helper struct.

For double buffering (`MAX_FRAMES_IN_FLIGHT = 2`):

- `std::vector<vk::raii::Semaphore> image_available_semaphores_` — one per frame in flight
- `std::vector<vk::raii::Semaphore> render_finished_semaphores_` — one per frame in flight
- `std::vector<vk::raii::Fence> in_flight_fences_` — one per frame in flight (start signalled)
- `uint32_t current_frame_ = 0` — index cycling `0..MAX_FRAMES_IN_FLIGHT-1`

#### 3. Command pool + command buffers

Create `vk::raii::CommandPool` for the graphics queue family, then allocate one
`vk::raii::CommandBuffer` per frame in flight.

#### 4. Basic render loop — clear screen to a colour

The main loop becomes:

```text
1. Wait for in_flight_fence[current_frame]
2. Acquire next swapchain image (image_available_semaphore[current_frame])
   - If OUT_OF_DATE: recreate swapchain, continue
3. Reset in_flight_fence[current_frame]
4. Reset + begin command buffer[current_frame]
5. Transition swapchain image: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL (pipeline barrier)
6. vkCmdBeginRendering with clear colour (e.g., dark teal: 0.0, 0.1, 0.15, 1.0)
7. vkCmdEndRendering
8. Transition swapchain image: COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC (pipeline barrier)
9. End + submit command buffer (wait: image_available, signal: render_finished)
10. Present (wait: render_finished)
    - If OUT_OF_DATE or SUBOPTIMAL: recreate swapchain
11. current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT
```

Use `vkCmdBeginRendering` / `vkCmdEndRendering` (dynamic rendering) — **no VkRenderPass**.
Use `vkCmdPipelineBarrier2` for image layout transitions.

#### 5. Handle window resize

- On `WindowEvent::Type::Resize`: set a `framebuffer_resized` flag
- After present (or on OUT_OF_DATE / SUBOPTIMAL): call `swapchain.recreate(new_width, new_height)`
- Skip frames while minimised (extent 0×0)

#### 6. Update `src/CMakeLists.txt`

Add `gpu_swapchain.cpp` to the source list.

#### 7. Run clang-format, build, test, commit, and PR

- `clang-format -i` on all changed C++ files
- `cmake --preset windows-msvc && cmake --build build/windows-msvc --config Debug`
- `ctest --test-dir build/windows-msvc -C Debug` — all existing tests still pass
- Manual verification: window opens with a solid colour, handles resize, ESC closes
- Branch: `feat/swapchain`
- PR against `main`

### Acceptance Criteria

- [ ] Swapchain created with MAILBOX present mode (FIFO fallback)
- [ ] Surface format: sRGB preferred (B8G8R8A8_SRGB)
- [ ] Image views created for all swapchain images
- [ ] Command pool + per-frame command buffers allocated
- [ ] Frame sync: semaphores + fences for double-buffered rendering
- [ ] Render loop clears screen to a visible colour (not black)
- [ ] Dynamic rendering used (`vkCmdBeginRendering`) — no VkRenderPass
- [ ] Image layout transitions via pipeline barriers
- [ ] Swapchain recreated on window resize and OUT_OF_DATE
- [ ] Minimised window handled gracefully (no crash, no busy loop)
- [ ] `vk::raii` for all new Vulkan objects — no manual destroy
- [ ] `[[nodiscard]]`, `constexpr`, explicit types, `//!` doxygen
- [ ] Code follows `.clang-format` and `STYLE.md`
- [ ] British spelling in all comments and documentation
- [ ] All existing tests still pass

---

## Phase 0 — Remaining Etapes (after Etape 3)

### Etape 4 — Triangle on screen

- VMA integration for vertex buffer memory allocation
- Graphics pipeline with `VkPipelineRenderingCreateInfo` (dynamic rendering, no VkRenderPass)
- Hardcoded colourful triangle (vertex + fragment shaders via Slang)
- Triangle on screen — Phase 0 complete

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
