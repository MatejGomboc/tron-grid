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

## Current Etape: 2 — Vulkan Instance, Validation, and Device

**Branch:** create from `main`, name `feat/vulkan-init`

**Goal:** Integrate Volk for dynamic Vulkan loading, create a Vulkan instance with validation
layers, set up a debug messenger, query physical devices, select the best GPU, create a logical
device with graphics + present queues, and wire up Vulkan surface creation in the window library.
After this etape, the application prints the selected GPU name and validation layer messages
appear in Debug builds.

**Read before starting:** `docs/ARCHITECTURE.md`, `STYLE.md`, `.clang-format`,
`CLAUDE.md` § Critical Rules (especially: `VK_NO_PROTOTYPES`, `vk::raii`, no manual destroy,
dynamic rendering extensions)

**Reference:** User's prior Vulkan project for the logical flow:
`github.com/MatejGomboc/Vulkan3DSimulator/blob/main/renderer.cpp`

### Dependencies to fetch

| Library | Purpose | Integration |
|---------|---------|-------------|
| Volk | Dynamic Vulkan function loading | `add_subdirectory`, define `VK_NO_PROTOTYPES` globally |
| vulkan-hpp | C++ Vulkan bindings with `vk::raii` | Header-only, from Vulkan SDK or fetched |

### Steps (do in order)

#### 1. Integrate Volk

- Fetch Volk via CMake `FetchContent` (or git submodule) into a known location
- Create `libs/volk/CMakeLists.txt` or add at root level — single `.cpp` with
  `#define VOLK_IMPLEMENTATION` + `#include <volk/volk.h>`
- Define `VK_NO_PROTOTYPES` as a **global** compile definition in root `CMakeLists.txt`
- Verify: project still compiles (Volk linked but not yet called)

#### 2. Integrate vulkan-hpp

- Fetch `vulkan-hpp` headers (from Vulkan SDK or `FetchContent`)
- Ensure `<vulkan/vulkan_raii.hpp>` is available as an include path
- Configure for dispatcher mode compatible with Volk (no static dispatcher — Volk provides
  the function pointers)

#### 3. Create `libs/gpu/` — the Vulkan initialisation brick

Create a new LEGO brick that owns the Vulkan instance and device.

```text
libs/gpu/
├── CMakeLists.txt                  # add_library(gpu STATIC ...)
├── include/gpu/
│   ├── instance.hpp                # gpu::Instance — Vulkan instance + debug messenger
│   └── device.hpp                  # gpu::Device — physical + logical device
├── src/
│   ├── instance.cpp                # volkInitialize, instance creation, debug messenger
│   └── device.cpp                  # physical device selection, logical device creation
└── tests/
    ├── CMakeLists.txt
    └── gpu_tests.cpp               # instance creation, device enumeration (headless)
```

Namespace: `gpu::`. Links: `volk`, `vulkan-hpp`.

#### 4. `gpu::Instance` — Vulkan instance + debug messenger

```cpp
namespace gpu {
class Instance {
    vk::raii::Context context_;          // Vulkan function loader
    vk::raii::Instance instance_;        // Vulkan instance (RAII)
    vk::raii::DebugUtilsMessengerEXT debug_messenger_;  // Debug only
};
}
```

Init sequence:

1. `volkInitialize()` — find Vulkan loader on the system
2. Check Vulkan version ≥ 1.3
3. Enumerate and verify required instance extensions:
   - `VK_KHR_surface`
   - `VK_KHR_win32_surface` (Windows) or `VK_KHR_xcb_surface` (Linux)
   - `VK_EXT_debug_utils` (Debug builds only)
4. Enumerate and verify validation layer: `VK_LAYER_KHRONOS_validation` (Debug only)
5. Create `vk::raii::Instance` with app info (API version 1.3, engine name "TronGrid")
6. `volkLoadInstance(instance)` — load instance-level function pointers
7. Create `vk::raii::DebugUtilsMessengerEXT` with callback that prints to `std::cerr`
   (severity: warning + error; types: general + validation + performance)

Use CMake generator expression for Debug detection — no `#ifdef DEBUG` in source code.
Instead, pass a `bool enable_validation` parameter to the constructor, and set it from
`CMakeLists.txt` or from `main()` based on build type.

#### 5. `gpu::Device` — physical + logical device

```cpp
namespace gpu {
class Device {
    vk::raii::PhysicalDevice physical_device_;
    vk::raii::Device device_;            // Logical device (RAII)
    vk::raii::Queue graphics_queue_;
    vk::raii::Queue present_queue_;
    uint32_t graphics_family_index_;
    uint32_t present_family_index_;
};
}
```

Physical device selection:

1. Enumerate physical devices via `instance.enumeratePhysicalDevices()`
2. For each device, find queue families supporting graphics (`VK_QUEUE_GRAPHICS_BIT`)
   and present (`vkGetPhysicalDeviceSurfaceSupportKHR`)
3. Prefer a single queue family that supports both (better performance)
4. Prefer discrete GPU over integrated
5. Reject devices that lack required extensions:
   - `VK_KHR_swapchain`
   - `VK_KHR_dynamic_rendering` (or Vulkan 1.3 core)
6. Print selected device name to stdout

Logical device creation:

1. Create queue create infos (graphics + present — may be same family)
2. Enable required device extensions: `VK_KHR_swapchain`, `VK_KHR_dynamic_rendering`
3. Enable `VkPhysicalDeviceDynamicRenderingFeatures` in the pNext chain
4. Create `vk::raii::Device`
5. `volkLoadDevice(device)` — load device-level function pointers
6. Retrieve `vk::raii::Queue` handles for graphics and present

#### 6. Wire up Vulkan surface creation in `libs/window/`

- Un-stub `Window::create_surface()` — now that Volk is available
- `win32_window.cpp`: call `vkCreateWin32SurfaceKHR` via Volk
- `xcb_window.cpp`: call `vkCreateXcbSurfaceKHR` via Volk
- The window library now links Volk (for the surface creation functions)
- Return a `VkSurfaceKHR` that the caller wraps in `vk::raii::SurfaceKHR`

#### 7. Update `src/main.cpp`

Wire everything together:

```cpp
auto window = window::create(config);
gpu::Instance instance(enable_validation, window_extensions);
auto surface = window->create_surface(*instance);  // VkSurfaceKHR
gpu::Device device(instance, surface);
std::cout << "GPU: " << device.name() << "\n";
// ... existing event loop ...
```

The exact API shape may vary — keep it clean and minimal.

#### 8. Run clang-format, build, test, commit, and PR

- `clang-format -i` on all changed C++ files
- `cmake --preset windows-msvc && cmake --build build/windows-msvc --config Debug`
- `ctest --preset windows-msvc-debug` — all tests pass (including new gpu_tests)
- Branch: `feat/vulkan-init`
- PR against `main`

### Acceptance Criteria

- [ ] `VK_NO_PROTOTYPES` defined globally
- [ ] Volk loads Vulkan dynamically — no static Vulkan linking
- [ ] `vk::raii` used for all Vulkan objects — no manual `vkDestroy*`
- [ ] Vulkan instance created with API version 1.3
- [ ] Validation layers active in Debug builds, silent in Release
- [ ] Debug messenger prints validation warnings/errors to stderr
- [ ] Physical device selected (prefer discrete GPU)
- [ ] Logical device created with graphics + present queues
- [ ] `VK_KHR_dynamic_rendering` enabled on the device
- [ ] Vulkan surface created from the window (Win32 / XCB)
- [ ] Selected GPU name printed to stdout
- [ ] `gpu_tests` pass (at minimum: instance creation in headless mode)
- [ ] Code follows `.clang-format` and `STYLE.md`
- [ ] British spelling in all comments and documentation
- [ ] No `#ifdef DEBUG` in source — use runtime `enable_validation` flag

---

## Phase 0 — Remaining Etapes (after Etape 2)

### Etape 3 — Swapchain

- VMA integration for memory allocation
- Swapchain setup (MAILBOX present mode, surface format selection)
- Swapchain image views
- Swapchain recreation on window resize

### Etape 4 — Triangle on screen

- Graphics pipeline with `VkPipelineRenderingCreateInfo` (dynamic rendering, no VkRenderPass)
- Command buffer recording with `vkCmdBeginRendering` / `vkCmdEndRendering`
- Frame synchronisation (fences + semaphores, double/triple buffering)
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
