# TODO & Development Journal

A shared task list and journal for humans and AI assistants working on TronGrid.

---

## Phase 3 — Mesh Shaders + Scene Architecture ✅

Completed 2026-03-28. Etapes 14 (PR #56), 15 (PR #59), 16 (PR #61), 17 (PR #63)

---

## Phase 4 — Procedural Tron Terrain ✅

Completed 2026-03-28. Etape 18 (PR #66), Etapes 19-20 (PR #68)

---

## Phase 5 — Acceleration Structures + Hard Shadows

**Goal:** Build acceleration structures and cast inline shadow rays via
`VK_KHR_ray_query` in the fragment shader. The terrain receives hard shadows
from itself — proving the RT infrastructure works end-to-end before adding
full PBR lighting in Phase 6.

**Why ray query, not a full RT pipeline?** Shadow rays only need a hit/miss
boolean. `VK_KHR_ray_query` traces rays inline in the fragment shader — no
SBT, no separate raygen/miss/closesthit shaders, no `vkCmdTraceRaysKHR`.
Much simpler, fewer moving parts, and the recommended approach per Khronos
and NVIDIA best-practice guides. The full `VK_KHR_ray_tracing_pipeline` is
reserved for reflections/GI in Phase 6.

**Visual target:** The existing wireframe terrain with sharp, pixel-perfect
shadows where elevated terrain blocks a directional light. Shadowed wireframe
edges become dimmer, shadowed faces become pure black. The Tron aesthetic is
preserved — no soft penumbrae.

### Etape 21 — RT Extension Bootstrap

Enable the Vulkan ray tracing extension chain and query device support.

**Required extensions (add to `device.cpp`):**

- `VK_KHR_acceleration_structure`
- `VK_KHR_ray_query`
- `VK_KHR_deferred_host_operations` (required dependency of AS extension)

**Required features (chain into `vk::PhysicalDeviceFeatures2`):**

- `VkPhysicalDeviceAccelerationStructureFeaturesKHR` — `accelerationStructure`
- `VkPhysicalDeviceRayQueryFeaturesKHR` — `rayQuery`
- `VkPhysicalDeviceBufferDeviceAddressFeaturesKHR` — `bufferDeviceAddress`
  (needed for AS device addresses; also enable on vertex/index buffers)

**Device scoring:** Reject GPUs that lack AS + ray query support (return -1
in `rateDevice`).

**Properties to query and log:**

- `VkPhysicalDeviceAccelerationStructurePropertiesKHR`:
  `minAccelerationStructureScratchOffsetAlignment`, `maxInstanceCount`.

**VMA change:** Add `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT` to
`VmaAllocatorCreateInfo::flags` in `allocator.cpp`. This causes VMA to
automatically add `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT` to all memory
blocks, so any buffer created with `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`
will work correctly.

**Buffer usage changes:** Vertex and index buffers need
`VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT` and
`VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR`
so the AS builder can read their device addresses.

**After this step:** application starts, selects an RT-capable GPU, logs AS
properties. No visible change yet.

### Etape 22 — BLAS for Terrain

Build a Bottom-Level Acceleration Structure (BLAS) from the terrain mesh.

**Approach:**

- Populate `VkAccelerationStructureGeometryTrianglesDataKHR` with device
  addresses for the terrain vertex/index buffers already on the GPU.
  Vertex format `VK_FORMAT_R32G32B32_SFLOAT`, stride `sizeof(Vertex)`,
  index type `VK_INDEX_TYPE_UINT32`.
- Set geometry flags `VK_GEOMETRY_OPAQUE_BIT_KHR` — terrain is fully opaque,
  so the driver can skip any-hit invocation (significant performance win).
- Build flags: `VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR` |
  `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR`.
  Terrain is static — optimise for traversal speed.
- Query build sizes via `vkGetAccelerationStructureBuildSizesKHR`.
- Allocate AS buffer (`VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR`)
  and scratch buffer (`VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`, aligned to
  `minAccelerationStructureScratchOffsetAlignment`).
- Create AS handle via `vkCreateAccelerationStructureKHR`.
- Record `vkCmdBuildAccelerationStructuresKHR`, barrier, destroy scratch.
- `vk::raii::AccelerationStructureKHR` exists natively in vulkan-hpp — no
  custom RAII wrapper needed. Bundle it with an `AllocatedBuffer` (for the
  AS storage buffer) in an `AllocatedAccelerationStructure` struct, similar
  to how `AllocatedImage` bundles an image with its VMA allocation.
- **Synchronisation2 barrier after build:**
  `srcStage = eAccelerationStructureBuildKHR`,
  `srcAccess = eAccelerationStructureWriteKHR` →
  `dstStage = eFragmentShader`,
  `dstAccess = eAccelerationStructureReadKHR`.

**BLAS compaction (saves 20-50% memory):**

- After build, `vkCmdWriteAccelerationStructuresPropertiesKHR` with
  `VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR`.
- Read back compacted size, allocate smaller AS buffer.
- `vkCmdCopyAccelerationStructureKHR` with `COMPACT` mode.
- Destroy original after copy completes.

**After this step:** BLAS built, compacted, and logged
("BLAS: N triangles, M bytes → K bytes compacted"). No visible change yet.

### Etape 23 — TLAS + Scene Integration

Build a Top-Level Acceleration Structure (TLAS) referencing the terrain BLAS.

**Approach:**

- One `VkAccelerationStructureInstanceKHR` per scene entity:
    - `transform`: entity model matrix (3×4 row-major).
    - `instanceCustomIndex`: entity ID (for future material lookup).
    - `mask`: `0xFF` (visible to all ray types).
    - `accelerationStructureReference`: BLAS device address.
    - `flags`: `VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR`
      (disable back-face culling for shadow rays hitting terrain from below).
- Upload instance data to a device-local buffer with
  `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
  VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR`.
- Build TLAS with `PREFER_FAST_TRACE` (static scene for now).
- Future phases: per-frame TLAS rebuild for moving objects, frustum-cull
  instances before TLAS build.

**After this step:** TLAS built and logged. No visible change yet.

### Etape 24 — Inline Shadow Rays in Fragment Shader

Add a `RayQuery` in the mesh shader's `fragMain` to cast shadow rays.

**Descriptor changes:**

- Add `RaytracingAccelerationStructure` (TLAS) to the descriptor set
  (`VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR`).
- Add a directional light direction as a push constant or UBO field
  (e.g., `float3 light_dir` normalised, pointing toward the light).

**Fragment shader changes (`mesh.slang`):**

```text
RaytracingAccelerationStructure scene_tlas;

[shader("fragment")]
float4 fragMain(MeshOutput input) : SV_Target
{
    // Existing wireframe colour computation...
    float3 colour = wireframeColour(input);

    // Shadow ray — offset origin along normal to prevent self-intersection.
    float3 origin = input.world_position + input.world_normal * 0.002;
    float3 direction = -light_dir;  // toward the light

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001;
    ray.TMax = 1000.0;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
             RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(scene_tlas, 0, 0xFF, ray);
    query.Proceed();

    float shadow = (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
                   ? 0.15 : 1.0;  // dim, not pure black — Tron glow persists
    colour *= shadow;

    return float4(colour, 1.0);
}
```

**Ray flags explained:**

- `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` — stop on first hit (fastest;
  we only need a boolean, not the closest intersection).
- `RAY_FLAG_SKIP_CLOSEST_HIT_SHADER` — no closest-hit shader needed.

**Performance notes:**

- Cull shadow rays for surfaces facing away from the light
  (`dot(N, L) <= 0`).
- `tMax` can be reduced for localised lights (not needed for directional).
- Self-intersection offset: `0.002` along normal + `tMin = 0.001`.
- **Cliff-edge gotcha:** Quantised terrain heights create vertical cliff
  faces. Use the flat face normal (already computed per-triangle) for the
  offset, not interpolated vertex normals — interpolated normals at cliff
  edges can point sideways into the cliff wall.

**MeshOutput changes:** The mesh shader must output `world_position` and
`world_normal` as additional interpolated fields for the fragment shader to
use as ray origin and normal offset.

**After this step:** hard shadows visible on the terrain. Phase 5 complete.

### Implementation Notes

**MeshOutput must grow.** The fragment shader needs `world_position` and
`world_normal` for ray origin and offset. Two options:

1. **Output from mesh shader** — add `float3 world_position` and
   `float3 world_normal` to `MeshOutput`. This adds 24 bytes per vertex.
   With 252 max output vertices, total per-vertex output grows from 28 bytes
   (position + bary) to 52 bytes. Check against
   `maxMeshOutputMemorySize` (typically 32 KB on NVIDIA → 32768 / 52 ≈ 630
   vertices — well within the 252 limit).
2. **Reconstruct from depth** — use inverse VP matrix + screen-space depth
   to reconstruct world position in the fragment shader. Avoids extra mesh
   outputs but requires the inverse VP in the UBO and is less precise at
   distance. The normal would still need to be output.

Option 1 is simpler and more robust. Recommended.

**Descriptor set layout changes.** The current layout has 7 bindings
(UBO + 6 SSBOs). Adding the TLAS makes it 8. Binding numbers:

- Binding 0: Camera UBO (existing)
- Bindings 1-6: SSBOs (existing)
- **Binding 7: TLAS** (`VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR`)

Update `pipeline.cpp` descriptor set layout, pool sizes, and add a
`bindTLAS()` method.

**CameraUBO changes.** Add `float3 light_dir` (normalised, pointing toward
the light source) and `float3 pad` for 16-byte alignment. The fragment
shader reads this to determine the shadow ray direction.

**Slang compilation flags.** The mesh.slang module already compiles to
SPIR-V 1.4. Ray query requires the `SPV_KHR_ray_query` extension. Add
`-capability spirv_1_4+GL_EXT_ray_query` or equivalent Slang flag to
`slangc` invocation in `CMakeLists.txt`. Verify `spirv-val` accepts
the output.

**Descriptor pool.** Currently allocates `frames_in_flight` UBOs and
`frames_in_flight * 6` storage buffers. Add
`frames_in_flight` acceleration structure descriptors to the pool.

**AllocatedAccelerationStructure struct.** Bundles:

- `AllocatedBuffer` — the AS storage buffer (VMA-owned).
- `vk::raii::AccelerationStructureKHR` — the AS handle (RAII-owned).

Destruction order: AS handle first, then buffer. The RAII wrappers handle
this automatically if the AS is destroyed before the buffer goes out of
scope.

### Acceptance Criteria

- [ ] `VK_KHR_acceleration_structure` + `VK_KHR_ray_query` enabled
- [ ] `bufferDeviceAddress` enabled; vertex/index buffers have AS input flags
- [ ] BLAS built from terrain mesh (opaque, fast-trace, compacted)
- [ ] TLAS built from scene instances
- [ ] Inline `RayQuery` in `fragMain` casts shadow rays toward directional light
- [ ] `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` for maximum performance
- [ ] Self-intersection prevention (normal offset + tMin epsilon)
- [ ] Surfaces facing away from light skip shadow ray (dot cull)
- [ ] Hard shadows visible on terrain
- [ ] Per-object frustum culling still works
- [ ] Proper doxygen, STYLE.md compliant, British spelling
- [ ] All existing + new tests pass on all CI presets
- [ ] Slang RT shaders compile to SPIR-V offline; `spirv-val` validates them
- [ ] CI passes (no GPU needed — shader compilation + validation only)
- [ ] **Phase 5 complete — hard shadows via inline ray query**

---

## Backlog

<!-- Ideas, improvements, and tasks for later phases. -->

---

## Journal

<!-- Reverse chronological — newest entries at the top. -->
<!-- Format: ### YYYY-MM-DD — Short title -->

### 2026-03-28 — Phase 4 complete: procedural Tron terrain

Barycentric wireframe rendering in mesh shader (PR #66), procedural terrain
generator with value noise heightmap and quantised heights (PR #68). Replaced
cube/sphere test scene with single terrain entity. Fixed Y-flip (removed manual
matrix negation, kept -fvk-invert-y). Codebase-wide {} brace initialisation,
include order fixes, struct member init. Meshlet triangles reduced from 124 to
84 for barycentric vertex duplication. CodeQL overflow fix. 68 PRs merged.

### 2026-03-28 — Phase 3 complete: mesh shaders + scene architecture

Entity/component scene (SoA), meshlet generation, mesh shader pipeline
(VK_EXT_mesh_shader: task + mesh + fragment), mixed cube/sphere scene with
per-object meshlet routing, LOD data structures, UV sphere generator.
13 code audit fixes. Logger deadlock fixed (std::jthread). 63 PRs merged.

### 2026-03-22 — Phases 0-2.1 complete

Phase 0 (triangle), Phase 1 (fly through cubes), Phase 2 (GPU-driven, 1000 cubes,
compute frustum culling, IndirectCount, GPU timestamps), Phase 2.1 (Clang-Tidy,
spirv-val, -Werror, sanitisers, GPU validation, 4 sync bugs fixed). 59 PRs merged.
