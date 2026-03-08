# Architecture

Technical architecture of the TronGrid renderer.

---

## Overview

TronGrid is a Vulkan-based rendering engine written in C++20. It uses dynamic Vulkan loading (Volk),
Slang for shader authoring, and targets modern discrete GPUs with full ray tracing support.

```text
┌────────────────────────────────────────────────────┐
│                    Application                     │
│  Window creation (Win32 / X11) · Input · Main loop │
├────────────────────────────────────────────────────┤
│                   Vulkan Backend                   │
│  Instance · Device · Swapchain · Command buffers   │
├────────────────────────────────────────────────────┤
│                  Render Pipeline                   │
│  Render passes · Graphics pipelines · Shaders      │
├────────────────────────────────────────────────────┤
│                   GPU Resources                    │
│  Buffers · Images · Descriptors (bindless)         │
├────────────────────────────────────────────────────┤
│                  Volk (loader)                     │
│  Dynamic Vulkan function pointer resolution        │
└────────────────────────────────────────────────────┘
```

## Key Design Decisions

| Decision | Choice | Why |
|----------|--------|-----|
| Vulkan loading | Volk (dynamic) | No static linking; `VK_NO_PROTOTYPES` defined globally |
| Shader language | Slang | Modern, composable; compiles to SPIR-V |
| Coordinate system | Right-handed, Y-up | Matches glTF and most authoring tools |
| Units | Metres | Physically-based lighting needs real-world scale |
| Colour space | Linear internal, sRGB output | Correct blending and light accumulation |
| HDR range | 16-bit float | Emissive glow needs headroom beyond [0, 1] |
| Meshlet size | 64 vertices, 124 triangles | NVIDIA optimal for mesh shaders |
| Descriptor model | Fully bindless | No rebinding between draws; GPU-driven compatible |
| Present mode | MAILBOX | Low latency, no tearing |

## Platform Layer

The platform layer handles window creation and input. There is no abstraction layer — each platform
has its own code path:

- **Windows:** Win32 API (`CreateWindowEx`, message pump)
- **Linux:** X11 / Xlib (`XCreateWindow`, event loop)

Platform-specific Vulkan surface creation uses `VK_USE_PLATFORM_WIN32_KHR` or `VK_USE_PLATFORM_XLIB_KHR`.

## Build System

CMake 3.16+ with Ninja Multi-Config. Five presets cover all supported compiler/platform combinations:

| Preset | OS | Compiler |
|--------|----|----------|
| `windows-msvc` | Windows | MSVC (cl) |
| `windows-clang-cl` | Windows | Clang-CL (MSVC ABI) |
| `windows-mingw` | Windows | MinGW-w64 (GCC) |
| `linux-x11-gcc` | Linux | GCC |
| `linux-x11-clang` | Linux | Clang |

```bash
cmake --preset <name>
cmake --build build/<name> --config Debug
```

## Directory Structure

```text
tron_grid/
├── .claude/             ← project instructions
├── .github/             ← CI workflows, templates, Vulkan SDK actions
├── docs/                ← extended documentation (you are here)
├── src/                 ← C++ source files
├── CMakeLists.txt       ← build configuration
├── CMakePresets.json    ← compiler/platform presets
├── .clang-format        ← code formatting rules
└── .editorconfig        ← editor settings
```

## Frame Synchronisation

The renderer uses double or triple buffering with fences and semaphores:

- **Image available semaphore** — signals when a swapchain image is ready for rendering
- **Render finished semaphore** — signals when rendering to the image is complete
- **In-flight fence** — prevents the CPU from submitting work for a frame that the GPU hasn't finished

## Future Architecture (Phases 2+)

Once the foundation is solid, the architecture will evolve towards:

- **GPU-driven rendering** — indirect draw calls, visibility culling on the GPU
- **Bindless resources** — all textures and buffers accessible via descriptor indexing
- **Mesh shading** — meshlet-based geometry processing, replacing the traditional vertex pipeline
- **Ray tracing** — acceleration structures for shadows and full global illumination
- **Frame streaming** — capturing rendered frames and piping them to the AI brain (Phase 9)
