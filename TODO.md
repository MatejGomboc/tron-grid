# TODO & Development Journal

A shared task list and journal for humans and AI assistants working on TronGrid.

---

## Completed Etapes

- **Etape 1** — Internal Library Scaffolding. Completed 2026-03-13. PR #18.
- **Etape 2** — Vulkan Instance, Validation, and Device. Completed 2026-03-13. PR #20.
- **Etape 3** — Swapchain, Command Buffers, and Clear Screen. Completed 2026-03-15. PRs #22, #23, #24.
- **Etape 4** — Triangle on Screen (Phase 0 Finale). Completed 2026-03-19. PR #26.
- **Etape 5** — Math Library. Completed 2026-03-22. PR #34.
- **Etape 6** — Depth Buffer, 3D Vertex Format, Descriptors. Completed 2026-03-22. PR #37.
- **Etape 7** — Camera, Input, Cube Scene (Phase 1 Finale). Completed 2026-03-22. PR #39.
- **Etape 8** — Object SSBO and Indirect Draw. Completed 2026-03-22. PR #43.
- **Etape 9** — Compute Frustum Culling. Completed 2026-03-22. PR #46.
- **Etape 10** — IndirectCount, GPU Timestamps. Completed 2026-03-22. PR #48.
- **Etape 11** — Clang-Tidy, spirv-val, /W4. Completed 2026-03-22. PR #50.
- **Etape 12** — Runtime Sanitisers, GPU Validation, Sync Fixes. Completed 2026-03-22. PR #51.
- **Etape 13** — CI Quality Gate, -Werror, Sanitiser Jobs. Completed 2026-03-22. PR #53.

---

## Phase 1 — Solid Foundation (Fly Through Cubes) ✅

Completed 2026-03-22. Etapes 5 (PR #34), 6 (PR #37), 7 (PR #39).

---

## Phase 2 — GPU-Driven Resources ✅

Completed 2026-03-22. Etapes 8 (PR #43), 9 (PR #46), 10 (PR #48).

---

## Phase 2.1 — Code Quality Infrastructure ✅

Completed 2026-03-22. Etapes 11 (PR #50), 12 (PR #51), 13 (PR #53).

---

## Backlog

<!-- Ideas, improvements, and tasks for later phases. -->

---

## Journal

<!-- Reverse chronological — newest entries at the top. -->
<!-- Format: ### YYYY-MM-DD — Short title -->

### 2026-03-22 — Phase 2.1 complete: code quality infrastructure

Clang-Tidy (.clang-tidy), spirv-val, slangc warnings-as-errors, /W4 + -Wall -Wextra
-Wpedantic + -Werror/WX (zero-warning policy). ASan+UBSan and TSan CMake presets + CI
jobs. GPU-Assisted, Synchronisation, and Best Practices Vulkan validation. Fixed 4
cross-frame sync bugs found by sync validation. MSVC debug leak detection. clangd VS
Code extension. STYLE.md quality tooling section. 14 tools across 4 layers.

### 2026-03-22 — Etape 9: Compute frustum culling

GPU compute shader culls objects outside the camera frustum before graphics pass.
cull.slang tests bounding spheres against 6 frustum planes (Gribb-Hartmann extraction),
atomically compacts visible indices. Separate compute pipeline + layout, vkCmdFillBuffer
reset, barriers (fill→compute, compute→draw). MathLib Frustum/extractFrustum/isInsideFrustum
with 5 unit tests. 63 math tests total.

### 2026-03-22 — Etape 8: GPU-driven rendering

Replaced CPU-driven per-object push constants with GPU-driven SSBO + indirect draw.
1000 cubes (10x10x10) rendered via single `vkCmdDrawIndexedIndirect`. Object transforms
in `StructuredBuffer<ObjectData>`, shader indexes via `SV_InstanceID`. Device features
enabled: `multiDrawIndirect`, `shaderDrawParameters`, descriptor indexing. Push constants
removed from pipeline layout. Vision concept art added to README.

### 2026-03-22 — Phase 1 complete

Fly through a 5x5x5 grid of cubes with WASD + mouse look. MathLib (Vec, Mat4, Quat,
projection, 57 unit tests), depth buffer, 3D vertex format (position + normal + UV),
camera UBO with descriptors, push constants for per-object transforms, free-flight
camera with quaternion orientation, cursor capture (Win32 + XCB), delta time,
index buffer, procedural cube mesh.

### 2026-03-22 — Phase 1 progress: math, depth, descriptors

MathLib header-only library (Vec2/3/4, Mat4, Quat, projection, 57 unit tests).
Depth buffer via VMA, 3D vertex format (position + normal + UV — meshlet/RT-ready),
camera UBO with per-frame descriptor sets, push constants for per-object model matrix.
Triangle now renders with perspective projection and depth testing.
Event-driven rendering with separate render thread. `MathLib::PI` constant.
`TestFixtureLib` → `TestingLib` rename.

### 2026-03-19 — Phase 0 complete

Triangle on screen with VMA, Slang shaders, dynamic rendering, logging library,
and comprehensive STYLE.md. All 5 CI presets pass. Codebase fully modernised:
camelCase functions, `m_` members, `vk::` scoped enums, brace initialisation,
GPL v3 licence headers, Qt-style doxygen.

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
