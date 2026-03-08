# TronGrid

Vulkan-based rendering engine for a digital world where AI creatures perceive and navigate through rendered images. This repo is the renderer. The AI brain lives in a separate repo.

## Quick Orientation

```text
tron_grid/
├── .claude/CLAUDE.md    ← you are here
├── .github/             ← CI workflows, Vulkan SDK setup actions, dependabot
├── .clang-format        ← LLVM-based, Allman braces, 4-space indent, 170 col
├── .editorconfig        ← 4-space indent, trim trailing whitespace
├── .gitignore
├── CMakeLists.txt       ← build config (finds Vulkan, sets platform defines)
├── CMakePresets.json    ← 5 presets: windows-msvc, windows-clang-cl, windows-mingw, linux-x11-gcc, linux-x11-clang
├── LICENCE              ← GPL v3
├── README.md            ← public-facing project overview
└── main.cpp             ← entry point (currently hello world placeholder)
```

## Rules

- **Language:** C++20. No exceptions.
- **Platforms:** Windows (Win32) and Linux (X11) only. No macOS. No Wayland. No mobile.
- **Spelling:** British English everywhere (colour, optimise, metres, synchronise, etc.). The LICENCE file content is untouchable (legal document).
- **Formatting:** Run `.clang-format`. Allman braces for functions/namespaces, 4-space indent, 170 column limit.
- **Vulkan loading:** Volk (dynamic). Always define `VK_NO_PROTOTYPES`. Never link Vulkan statically.
- **Shaders:** Slang (not GLSL/HLSL directly).
- **Build:** CMake 3.16+ with Ninja Multi-Config. Use presets: `cmake --preset <name>`, `cmake --build build/<name> --config Debug|Release`.
- **CI:** GitHub Actions. 5 matrix jobs matching the 5 presets. Vulkan SDK 1.4.335.0.
- **Licence:** GPL v3-or-later.
- **Don't over-engineer.** Keep it simple. No abstractions until there's a concrete second use case.

## Building

```bash
# Windows (from VS Developer Command Prompt or with MSVC in PATH)
cmake --preset windows-msvc
cmake --build build/windows-msvc --config Debug

# Linux
sudo apt-get install libx11-dev
cmake --preset linux-x11-gcc
cmake --build build/linux-x11-gcc --config Debug
```

## Target Hardware

- NVIDIA RTX 4090 (Ada Lovelace), 16 GB VRAM
- Goal: 4K @ 60+ FPS with full ray tracing

## Key Design Decisions

| Decision           | Choice                        | Why                                    |
|--------------------|-------------------------------|----------------------------------------|
| Coordinate system  | Right-handed, Y-up            | Matches glTF, most tools               |
| Units              | Metres                        | Physically-based lighting              |
| Colour space       | Linear internal, sRGB output  | Correct blending                       |
| HDR range          | 16-bit float                  | Emissive glow needs headroom           |
| Meshlet size       | 64 verts, 124 triangles       | Nvidia optimal                         |
| Descriptor model   | Fully bindless                | No rebinding, GPU-driven               |
| Present mode       | MAILBOX                       | Low latency, no tearing                |

## Roadmap

Currently in **Phase 0 — Foundation** (triangle on screen).

| Phase | Goal                    | Milestone                        |
|-------|-------------------------|----------------------------------|
| 0     | Prove the toolchain     | Triangle on screen               |
| 1     | Solid foundation        | Fly through cubes                |
| 2     | GPU-driven resources    | 1000 objects, 1 draw call        |
| 3     | Mesh shaders            | Meshlet rendering                |
| 4     | Procedural geometry     | Tron-style CSG scene             |
| 5     | Acceleration structures | Hard shadows                     |
| 6     | Physically-based RT     | Full RT lighting                 |
| 7     | Post processing         | Bloom, tonemapping               |
| 8     | Optimisation            | 4K @ 60+ rock-solid              |
| 9     | AI integration          | Frame streaming API for AI brain |

Phases 3–4 and 5–6 can be developed in parallel after phase 2.

```text
Phase 0 --> 1 --> 2 --+--> 3 --> 4
                      |
                      +--> 5 --> 6

3 + 6 --> 7 --> 8 --> 9
```

## Phase 0 Checklist

- [ ] Integrate Volk for dynamic Vulkan loading
- [ ] Create platform window (Win32 / X11)
- [ ] Vulkan instance + debug messenger
- [ ] Physical device selection (prefer discrete GPU)
- [ ] Logical device + queue creation
- [ ] Swapchain setup (MAILBOX present mode)
- [ ] Render pass + framebuffers
- [ ] Graphics pipeline (vertex + fragment, hardcoded triangle)
- [ ] Command buffer recording + submission
- [ ] Frame synchronisation (fences + semaphores)
- [ ] Triangle on screen
