# TODO & Development Journal

A shared task list and journal for humans and AI assistants working on TronGrid.

---

## Phase 3 — Mesh Shaders + Scene Architecture

**Goal:** Replace the traditional vertex pipeline with mesh shaders (`VK_EXT_mesh_shader`),
introduce a minimal entity/component scene structure, and implement meshlet-based rendering
with per-meshlet frustum culling in the task shader. After this phase, the renderer uses
the modern mesh shader pipeline with proper scene management — the foundation for adaptive
LOD (Nanite-style) and procedural geometry (Phase 4).

**Architectural shift:** The GPU decides both *what* to render (frustum culling) and *at what
detail level* (LOD selection). The CPU just uploads scene data and says "go."

**Design for the future:**

- Entity/component model is the backbone for Phase 4 (procedural geometry), Phase 9 (AI
  perception), and Phase 10 (multiplayer entity replication)
- Meshlet data structure prepares for adaptive LOD (add LOD levels per mesh later)
- Task shader culling replaces the compute cull pass (one pipeline instead of two)
- The scene database (flat arrays of components) maps directly to GPU SSBOs

**Architecture notes (vs. Vulkan tutorial recommendations):**

- The Vulkan tutorial recommends `std::vector<std::unique_ptr<Component>>` with virtual
  `Update()` per entity. We use **Structure of Arrays** (flat parallel arrays) instead —
  this is the tutorial's own "data-oriented design" pattern, optimised for GPU upload
  and cache-friendly CPU iteration. No virtual functions, no heap allocation per component.
- The tutorial's resource management chapter recommends handle-based indirection for
  GPU resources. Our `AllocatedBuffer`/`AllocatedImage` RAII wrappers already provide
  this — typed handles with automatic cleanup. No raw Vulkan handles exposed.
- The tutorial's event system uses a polymorphic `Event` base class with `dynamic_cast`
  dispatch. Our `SignalsLib::Signal<T>` is typed at compile time — no inheritance, no
  RTTI, no runtime type checking. Thread-safe by default.
- Resource streaming and async loading (tutorial chapter 4) are deferred to Phase 8
  (optimisation). Phase 3 uses procedurally generated meshlets — no file I/O needed.

### Etape 14 — Entity/Component Scene Structure

Create a minimal entity/component framework. No inheritance trees, no virtual Update() —
just plain data arrays the GPU can read.

**New files:**

```text
src/
├── scene.hpp      # Scene class — flat arrays of entities and components
├── entity.hpp     # Entity — ID + component mask (no inheritance)
├── components.hpp # Transform, MeshID, Bounds — plain structs
```

**Design:**

- `Entity` is a lightweight ID (uint32_t index into component arrays)
- Components are stored in parallel arrays (Structure of Arrays), not per-entity vectors:
    - `std::vector<Transform>` — position (Vec3), orientation (Quat), scale (Vec3)
    - `std::vector<MeshID>` — which mesh this entity uses (uint32_t index)
    - `std::vector<Bounds>` — bounding sphere (centre + radius)
- `Scene` class owns the arrays + provides add/remove/query
- No virtual functions, no `std::unique_ptr<Component>`, no `dynamic_cast` — pure data
- The GPU-side SSBOs (ObjectData, ObjectBounds) are uploaded from these arrays
- This replaces the hardcoded cube grid in main.cpp

**After this step:** same 1000 cubes, but managed through the Scene API instead of
hardcoded loops. Visual output unchanged — this is a refactoring etape.

### Etape 15 — Meshlet Data Structure + Generation

Split mesh geometry into meshlets — small fixed-size chunks that mesh shaders process.

**Meshlet structure:**

```cpp
struct Meshlet {
    uint32_t vertex_offset;    //!< Offset into the meshlet vertex index buffer.
    uint32_t vertex_count;     //!< Number of vertices in this meshlet (max 64).
    uint32_t triangle_offset;  //!< Offset into the meshlet triangle index buffer.
    uint32_t triangle_count;   //!< Number of triangles in this meshlet (max 124).
};

struct MeshletBounds {
    Vec3 centre;  //!< Bounding sphere centre (local space).
    float radius; //!< Bounding sphere radius.
};
```

**Meshlet generation:**

- Take the existing cube mesh (24 vertices, 36 indices = 12 triangles)
- A single cube fits in one meshlet (12 triangles < 124 max)
- For future meshes: implement a simple greedy meshlet builder that partitions
  triangles into groups of up to 124, sharing up to 64 vertices
- Store meshlet vertex indices as uint8_t offsets (local to the meshlet, max 64)
- Store meshlet triangle indices as packed uint8_t triplets

**GPU buffers (SSBOs):**

- Meshlet descriptor buffer: array of `Meshlet` structs
- Meshlet bounds buffer: array of `MeshletBounds` (for per-meshlet culling)
- Vertex position buffer: all mesh vertices (read by mesh shader)
- Meshlet vertex index buffer: uint8_t indices into the vertex buffer
- Meshlet triangle index buffer: uint8_t triangle indices (3 per triangle)

**After this step:** meshlet data is generated on the CPU and uploaded to SSBOs.
Not yet rendered — the mesh shader comes in Etape 16.

### Etape 16 — Mesh Shader Pipeline

Replace the traditional vertex shader with a mesh shader pipeline.

**Device features to enable:**

- `VkPhysicalDeviceMeshShaderFeaturesEXT::meshShader`
- `VkPhysicalDeviceMeshShaderFeaturesEXT::taskShader`
- Extensions: `VK_EXT_mesh_shader`, `VK_KHR_spirv_1_4`,
  `VK_KHR_shader_float_controls`

**Slang shaders:**

- `mesh.slang` — mesh shader: one workgroup per meshlet
    - Reads meshlet descriptor to know vertex/triangle counts and offsets
    - Reads vertex positions from the vertex SSBO
    - Reads per-object transform from the object SSBO (via object index)
    - Outputs `SetMeshOutputCounts(vertex_count, triangle_count)`
    - Writes `vertices[i].position = mul(mvp, vertex_pos)`
    - Writes `primitives[i] = uint3(idx0, idx1, idx2)`
    - Uses HLSL mesh shader semantics: `[outputtopology("triangle")]`,
      `[numthreads(64, 1, 1)]`

- `task.slang` — task shader: one workgroup per batch of meshlets
    - Reads meshlet bounds + object transform
    - Tests each meshlet's bounding sphere against the frustum (6 plane test)
    - If visible: includes in the dispatch via `DispatchMesh(visible_count, 1, 1)`
    - Passes visible meshlet indices to the mesh shader via task payload

**Pipeline changes:**

- Create a new graphics pipeline with mesh shader stages (no vertex input state)
- Pipeline stages: task shader → mesh shader → fragment shader
- Fragment shader unchanged (still outputs normal as colour)
- Remove the old vertex shader pipeline
- Remove the compute cull pipeline (task shader does culling now)

**Draw command:**

- Replace `vkCmdDrawIndexedIndirectCount` with `vkCmdDrawMeshTasksIndirectEXT`
- Or for the initial version: `vkCmdDrawMeshTasksEXT(meshlet_count, 1, 1)`
- Each task shader workgroup processes a batch of meshlets for one object

**After this step:** 1000 cubes rendered via mesh shaders with per-meshlet frustum
culling in the task shader. Visual output same as before — normals as colour, depth
testing, free-flight camera. But the geometry pipeline is fully modern.

### Etape 17 — Multiple Mesh Types + LOD Foundation

Add a second mesh type and lay the foundation for adaptive LOD.

**Second mesh type:**

- Add a procedural sphere (icosphere or UV sphere) alongside the cube
- Scene contains a mix: some entities are cubes, some are spheres
- `MeshID` component determines which mesh each entity uses
- Meshlet buffers contain both meshes, offset by mesh region

**LOD structure (data only — no LOD selection yet):**

```cpp
struct MeshLOD {
    uint32_t meshlet_offset; //!< First meshlet index for this LOD level.
    uint32_t meshlet_count;  //!< Number of meshlets in this LOD level.
};

struct MeshDescriptor {
    uint32_t lod_count;      //!< Number of LOD levels.
    MeshLOD lods[4];         //!< LOD levels (0 = highest detail).
};
```

- Generate 2 LOD levels for each mesh (full detail + simplified)
- Store in the meshlet buffers with offset ranges
- Task shader currently always selects LOD 0 (highest detail)
- The LOD selection logic comes in Phase 8 (optimisation) when we have
  screen-space metrics and more complex scenes to justify it

**After this step:** mixed cube/sphere scene with meshlet rendering. LOD data
structures are in place but LOD 0 is always used. **Phase 3 complete — meshlet
rendering with scene architecture.**

### Acceptance Criteria

- [ ] Entity/component scene structure (flat arrays, no inheritance)
- [ ] Scene class manages entities with Transform, MeshID, Bounds components
- [ ] Meshlet data structure (64 verts, 124 triangles max per meshlet)
- [ ] Meshlet generation from mesh geometry
- [ ] Per-meshlet bounding spheres
- [ ] `VK_EXT_mesh_shader` enabled (task + mesh shader features)
- [ ] Mesh shader renders meshlets from SSBO data
- [ ] Task shader performs per-meshlet frustum culling
- [ ] Compute cull pipeline removed (task shader replaces it)
- [ ] Multiple mesh types (cube + sphere) in the same scene
- [ ] MeshLOD data structure prepared (LOD 0 always selected for now)
- [ ] `vk::raii` for all new Vulkan objects
- [ ] Proper doxygen, STYLE.md compliant, British spelling
- [ ] All existing + new tests pass on all CI presets
- [ ] **Phase 3 complete — meshlet rendering with scene architecture**

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
