# TODO & Development Journal

A shared task list and journal for humans and AI assistants working on TronGrid.

---

## Completed Etapes

- **Etape 1** — Internal Library Scaffolding. Completed 2026-03-13. PR #18.
- **Etape 2** — Vulkan Instance, Validation, and Device. Completed 2026-03-13. PR #20.
- **Etape 3** — Swapchain, Command Buffers, and Clear Screen. Completed 2026-03-15. PRs #22, #23, #24.
- **Etape 4** — Triangle on Screen (Phase 0 Finale). Completed 2026-03-19. PR #26.

---

## Phase 1 — Solid Foundation (Fly Through Cubes)

**Goal:** Build the core 3D infrastructure — math, camera, depth buffer, uniform buffers,
descriptors, and a procedural cube scene. After this phase, the user flies through a grid
of cubes with WASD + mouse look. The vertex format and descriptor model are designed for
future mesh shader (Phase 3) and ray tracing (Phase 5-6) compatibility.

**Design for the future:**

- Vertex format: position + normal + UV (not just position + colour) — meshlet-ready,
  RT-ready (normals needed for shading, UVs for texturing)
- Push constants for per-object model transform — GPU builds the matrix, CPU just sends
  position + rotation + scale. This scales to thousands of objects in Phase 2
- Bindless-ready descriptor layout — start simple (1 UBO for camera VP matrix), but the
  pipeline layout can grow to bindless arrays later
- Depth buffer — required for rasterisation now and ray tracing comparison later

### Etape 5 — Math Library

Create `libs/math/` with namespace `MathLib`. Header-only, minimal — the GPU does the
heavy lifting, the CPU just needs enough to set up camera transforms.

```text
libs/math/
├── CMakeLists.txt
├── include/math/
│   ├── vec.hpp          # Vec2, Vec3, Vec4
│   ├── mat.hpp          # Mat4
│   ├── quat.hpp         # Quat (quaternion)
│   └── projection.hpp   # perspective(), lookAt()
└── tests/
    ├── CMakeLists.txt
    └── math_tests.cpp
```

**Types:**

- `Vec2` — 2D vector (UV coordinates)
- `Vec3` — 3D vector (position, direction, colour, scale)
- `Vec4` — 4D vector (homogeneous coordinates, RGBA)
- `Mat4` — 4x4 matrix (MVP, view, projection)
- `Quat` — quaternion (camera orientation, smooth interpolation)

**Functions:**

- `perspective(fov_y, aspect, near, far)` → `Mat4`
- `lookAt(eye, target, up)` → `Mat4`
- `Mat4::translate(Vec3)`, `Mat4::rotate(Quat)`, `Mat4::scale(Vec3)`
- `Quat::fromAxisAngle(Vec3, float)`, `Quat::toMat4()`
- Basic arithmetic: `+`, `-`, `*`, dot, cross, normalise, length
- All `constexpr` where possible, `[[nodiscard]]` on all getters

**Unit tests (critical — subtle bugs here show up as "scene looks wrong" with no error):**

- Vec3/Vec4: dot, cross, normalise, length, operator arithmetic
- Mat4: identity, multiply, transpose, inverse, translate/rotate/scale
- Quat: from axis-angle, to Mat4, slerp, normalise, multiply, identity
- Projection: perspective matrix produces correct clip-space coordinates
  (test near/far plane mapping, aspect ratio, FOV)
- View: lookAt matrix produces expected eye/target/up orientation
- Edge cases: zero-length normalise, degenerate quaternion, near=far, aspect=0

**After this step:** math library compiles, unit tests prove correctness of projection,
view matrix, quaternion rotation, and basic vector/matrix operations.

### Etape 6 — Depth Buffer, 3D Vertex Format, Descriptors

**Depth buffer:**

- Create a depth image via VMA (`VK_FORMAT_D32_SFLOAT` preferred, fallback to
  `VK_FORMAT_D24_UNORM_S8_UINT`)
- Create depth image view
- Add depth attachment to `vk::RenderingInfo` in `recordFrame()`
- Enable depth test + depth write in the graphics pipeline
- Recreate depth image on swapchain resize (same dimensions as colour attachment)

**3D vertex format (meshlet/RT-ready):**

```cpp
struct Vertex {
    float position[3]; // Location 0: POSITION
    float normal[3];   // Location 1: NORMAL
    float uv[2];       // Location 2: TEXCOORD0
};
```

- Update pipeline vertex input: 3 attributes (position R32G32B32, normal R32G32B32, UV R32G32)
- Update Slang shader to accept the new format
- For now, the fragment shader can use the normal as colour (visualise normals) until
  we have proper lighting

**Uniform buffer + descriptors:**

- `struct CameraUBO { Mat4 view; Mat4 projection; }` — uploaded once per frame
- Per-frame uniform buffers via VMA (persistent mapped, `HOST_ACCESS_SEQUENTIAL_WRITE`)
- Descriptor set layout: binding 0 = `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`, vertex stage
- Descriptor pool + per-frame descriptor sets
- Update pipeline layout to include the descriptor set layout
- Push constant range for per-object model matrix (`Mat4`, 64 bytes, vertex stage)
- Update Slang vertex shader: `MVP = projection * view * model`

**After this step:** the triangle (now with normals) renders with a proper perspective
projection and depth testing. The camera is at a fixed position looking at the origin.

### Etape 7 — Camera, Input, Cube Scene

**Camera class (`src/camera.hpp`):**

- Position (`Vec3`), orientation (`Quat`), FOV, near/far planes
- `viewMatrix()` → `Mat4` (from position + orientation)
- `projectionMatrix(aspect)` → `Mat4` (perspective)
- `moveForward(delta)`, `moveRight(delta)`, `moveUp(delta)` — in local space
- `rotate(yaw, pitch)` — quaternion-based, no gimbal lock

**Input handling:**

- WASD for movement (forward/back/left/right), Space/Shift for up/down
- Mouse movement for look direction (yaw/pitch)
- Input state tracking: maintain a key-state map (pressed/released) on the render
  thread — camera movement reads "is W held?" not "W was pressed". Forward KeyDown,
  KeyUp, and MouseMove events to the render thread via `Signal<InputEvent>` or extend
  `RenderEvent` with input payload
- Mouse capture: right-click to engage FPS-style mouse look (hide cursor, capture
  relative movement). Win32: `SetCapture()` + `ShowCursor(FALSE)` + `ClipCursor()`.
  XCB: `xcb_grab_pointer()`. Right-click again or ESC to release
- Delta time: `std::chrono::steady_clock` in the render thread measures time between
  frames for frame-rate-independent movement speed

**Procedural cube mesh:**

- 24 vertices (4 per face, each with unique normal for flat shading)
- 36 indices (6 per face, 2 triangles each)
- Index buffer via VMA staging upload (same pattern as vertex buffer)
- Update draw call: `cmd.drawIndexed(36, 1, 0, 0, 0)`

**Cube grid scene:**

- N x N x N grid of cubes (e.g., 5 x 5 x 5 = 125 cubes)
- Each cube has a unique position (model matrix via push constants)
- Draw loop: for each cube, push model matrix, draw indexed
- Spacing between cubes so the camera can fly through

**After this step:** WASD + mouse to fly through a grid of cubes with perspective
projection and depth testing. **Phase 1 complete — fly through cubes.**

### Acceptance Criteria

- [ ] `libs/math/` — header-only math library with Vec2/3/4, Mat4, Quat, projection
- [ ] All math operations have unit tests (CTest)
- [ ] Depth buffer created via VMA, recreated on resize
- [ ] 3D vertex format: position + normal + UV (meshlet/RT-ready)
- [ ] Uniform buffer with camera VP matrix, updated per frame
- [ ] Descriptor set layout + pool + per-frame sets
- [ ] Push constants for per-object model matrix
- [ ] Camera class with WASD + mouse look (quaternion-based)
- [ ] Frame-rate-independent movement via delta time
- [ ] Procedural cube mesh with index buffer
- [ ] Grid of cubes (at least 5 x 5 x 5)
- [ ] Fly through the cube grid smoothly
- [ ] `vk::raii` for all new Vulkan objects — no manual destroy
- [ ] Proper doxygen, STYLE.md compliant, British spelling
- [ ] All existing + new tests pass on all 5 CI presets
- [ ] **Phase 1 complete — fly through cubes**

---

## Backlog

<!-- Ideas, improvements, and tasks for later phases. -->

---

## Journal

<!-- Reverse chronological — newest entries at the top. -->
<!-- Format: ### YYYY-MM-DD — Short title -->

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
