# TronGrid

A modern Vulkan-based rendering engine optimised for the Tron aesthetic — clean geometry, emissive materials, reflective surfaces, neon glow.

Designed as the visual foundation for [Project MOTE](https://github.com/MatejGomboc/mote): a digital world where AI creatures perceive and navigate through rendered images rather than direct scene graph access.

## Status

Early development. Currently building Vulkan infrastructure (Phase 0).

## Platforms

| Platform | Windowing | Status |
|----------|-----------|--------|
| Windows  | Win32     | Active |
| Linux    | X11       | Active |

## Requirements

- **Vulkan SDK** 1.3+ ([LunarG](https://vulkan.lunarg.com/))
- **C++20** compiler (MSVC, GCC, or Clang)
- **CMake** 3.16+
- **Ninja** build system

### Target Hardware

- **GPU:** NVIDIA RTX 4090 (Ada Lovelace)
- **VRAM:** 16 GB
- **Target:** 4K @ 60+ FPS with full ray tracing

### Required Vulkan Extensions

```
VK_EXT_mesh_shader              // Task + Mesh shaders
VK_KHR_acceleration_structure   // RT acceleration structures
VK_KHR_ray_tracing_pipeline     // RT pipeline
VK_KHR_deferred_host_operations // Async AS builds
VK_KHR_buffer_device_address    // GPU pointers for bindless
VK_EXT_descriptor_indexing      // Bindless resources
```

## Building

### Windows (MSVC)

```bash
cmake --preset windows-msvc
cmake --build build/windows-msvc --config Debug
```

### Windows (Clang-CL)

```bash
cmake --preset windows-clang-cl
cmake --build build/windows-clang-cl --config Debug
```

### Linux (GCC)

```bash
sudo apt-get install libx11-dev
cmake --preset linux-x11-gcc
cmake --build build/linux-x11-gcc --config Debug
```

### Linux (Clang)

```bash
sudo apt-get install libx11-dev
cmake --preset linux-x11-clang
cmake --build build/linux-x11-clang --config Debug
```

## Design Principles

1. **GPU-Driven** — Minimal CPU involvement in rendering decisions
2. **Bindless** — No per-draw descriptor rebinding
3. **Procedural** — Geometry defined mathematically, tessellated on GPU
4. **Physically-Based** — Ray traced lighting, no manual tricks
5. **Modular** — Clean separation of concerns for maintainability
6. **Incremental** — Every phase produces visible results

## Architecture

```
APPLICATION LAYER
  Scene Graph, Entity System, Camera, Input, MOTE Interface
                              |
                              v
SCENE REPRESENTATION
  Procedural Defs (NURBS, CSG, SDF)  |  Materials (PBR)  |  Lights (point, line, area)
                              |
                              v
GEOMETRY PROCESSING  (Compute Shaders)
  Tessellation --> Meshlet Builder --> GPU Scene Database
                              |
                   +----------+----------+
                   v                     v
        ACCELERATION STRUCTURES    VISIBILITY PASS
          BLAS/TLAS (async)       Task/Mesh/Fragment Shaders
                   |                     |
                   +----------+----------+
                              v
                    RAY TRACING PASS
          Shadows, Reflections, Emissive, Ambient GI
                              |
                              v
                     POST PROCESSING
           Bloom, Tonemapping, Chromatic Aberration
                              |
                              v
                       PRESENTATION
              Swapchain acquire --> Blit --> Present
```

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Coordinate system | Right-handed, Y-up | Matches glTF, most tools |
| Units | Metres | Physically-based lighting |
| Colour space | Linear internal, sRGB output | Correct blending |
| HDR range | 16-bit float | Emissive glow needs headroom |
| Meshlet size | 64 verts, 124 triangles | Nvidia optimal |
| Descriptor model | Fully bindless | No rebinding, GPU-driven |
| Present mode | MAILBOX | Low latency, no tearing |
| Shader language | Slang | Modern, modular, multi-target |
| Vulkan loader | Volk | Dynamic loading, no link dependency |

## Build Plan

| Phase | Goal | Milestone |
|-------|------|-----------|
| 0 - Foundation | Prove the toolchain | Triangle on screen |
| 1 - Infrastructure | Solid foundation | Fly through cubes |
| 2 - Bindless | GPU-driven resources | 1000 objects, 1 draw call |
| 3 - Mesh Shaders | Replace vertex pipeline | Meshlet rendering |
| 4 - Compute Geometry | Procedural geometry | Tron-style CSG scene |
| 5 - RT Setup | Acceleration structures | Hard shadows |
| 6 - RT Lighting | Physically-based lighting | Full RT lighting |
| 7 - Post Processing | The Tron *look* | Bloom, tonemapping |
| 8 - Optimisation | 4K@60+ performance | Rock-solid renderer |
| 9 - MOTE Prep | AI vision interface | Frame streaming API |

```
Phase 0 --> Phase 1 --> Phase 2 --+--> Phase 3 --> Phase 4
                                  |
                                  +--> Phase 5 --> Phase 6

Phase 3 + Phase 6 --> Phase 7 --> Phase 8 --> Phase 9
```

Phases 3-4 (Mesh Shaders / Geometry) and Phases 5-6 (RT) can be developed in parallel.

## Frame Data Flow

```
1. UPDATE      Scene changes, camera update
2. GEOMETRY    Re-tessellate dirty objects, update meshlet buffers
3. CULL/DRAW   Task shader culls, mesh shader emits, fragment writes vis buffer
4. BUILD AS    Update BLAS for moved objects, rebuild TLAS
5. TRACE       Shadow rays, reflection rays, shade and accumulate
6. POST        Bloom, tonemap, output to swapchain
```

## References

- [Vulkan Tutorial](https://vulkan-tutorial.com)
- [vkguide.dev](https://vkguide.dev)
- [Sascha Willems Vulkan Samples](https://github.com/SaschaWillems/Vulkan)
- [meshoptimizer](https://github.com/zeux/meshoptimizer)
- [Slang Shader Language](https://shader-slang.org)
- Ray Tracing Gems I & II
- Granite renderer (Hans-Kristian Arntzen)

## The Vision

> A digital creature will wake up in this world.
> It will see neon lines against infinite black.
> It will learn that glowing boundaries mean "wall."
> It will discover movement, space, self.
>
> The first AI to achieve grounded spatial awareness
> won't stumble through a grey test scene —
> it will blaze across a light cycle grid,
> reflections trailing behind it,
> living in a world that looks like the future we were promised.

## Licence

Copyright © 2026 Matej Gomboc <https://github.com/MatejGomboc/tron_grid>.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

See the attached [LICENCE](LICENCE) file for more info.
