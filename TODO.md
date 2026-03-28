# TODO & Development Journal

A shared task list and journal for humans and AI assistants working on TronGrid.

---

## Phase 3 — Mesh Shaders + Scene Architecture ✅

Completed 2026-03-28. Etapes 14 (PR #56), 15 (PR #59), 16 (PR #61), 17 (PR #63)

---

## Phase 4 — Procedural Tron Terrain

**Goal:** Replace the cube/sphere test scene with a procedural Tron-style terrain.
Dark triangular mesh with glowing wireframe edges — the iconic Grid aesthetic.
No textures, no materials system — pure procedural in the fragment shader.

**Visual target:** Dark angular terrain with bright neon wireframe edges (cyan/orange).
Triangle interiors are near-black. The glow effect comes later (Phase 7 bloom) — for
now, bright edge lines on dark faces already look distinctly Tron.

### Etape 18 — Wireframe Fragment Shader

Update the mesh + fragment shader to render wireframe edges using barycentric coordinates.

**Approach:** The mesh shader assigns barycentric coordinates (1,0,0), (0,1,0), (0,0,1)
to each triangle's 3 vertices as a per-vertex output. The fragment shader checks if any
barycentric component is near zero — that's an edge. No extension needed
(`VK_KHR_fragment_shader_barycentric` exists but is unnecessary — mesh shader outputs
barycentrics directly).

**Fragment shader logic:**

```text
float edge = min(bary.x, min(bary.y, bary.z));
float wire = smoothstep(0.0, WIRE_WIDTH, edge);
colour = mix(EDGE_COLOUR, FACE_COLOUR, wire);
```

- `EDGE_COLOUR`: bright neon (e.g., cyan `(0.0, 0.8, 1.0)` or orange `(1.0, 0.5, 0.0)`)
- `FACE_COLOUR`: near-black `(0.02, 0.02, 0.04)` — the Tron void
- `WIRE_WIDTH`: controls edge thickness (adjustable via push constant or constant)
- Change clear colour from dark teal to near-black `(0.01, 0.01, 0.02)`

**MeshOutput struct update:** Add `float3 barycentric` field. The mesh shader assigns
`(1,0,0)`, `(0,1,0)`, `(0,0,1)` to the 3 vertices of each triangle.

**After this step:** existing cubes/spheres render with glowing wireframe edges on dark
faces. Instant Tron aesthetic — even before terrain.

### Etape 19 — Procedural Terrain Generator

Generate a heightmap-based terrain mesh with the angular, crystalline Tron look.

**Terrain generator (`src/terrain.hpp/cpp`):**

- Input parameters: grid size (e.g., 64x64), tile spacing, height scale
- Generate a flat grid of vertices with XZ positions
- Apply height displacement using layered noise (Perlin or value noise)
- Quantise heights to discrete levels for the angular/terraced look
- Generate triangle indices (2 triangles per grid cell)
- Output: `std::vector<MathLib::Vec3>` positions + `std::vector<uint32_t>` indices
- Normals computed per-face (flat shading — matches the angular aesthetic)

**Vertex format:** Same `Vertex` struct (position + normal + UV). Normals are per-face
for flat shading. UVs map to world XZ for potential future grid-line texturing.

**Meshlet generation:** Run `buildMeshlets()` on the terrain — it naturally partitions
into meshlets. A 64x64 terrain = 8192 triangles ≈ 66 meshlets.

**After this step:** terrain mesh generated and meshletised on the CPU.

### Etape 20 — Tron Grid Scene

Replace the cube/sphere test scene with the terrain.

**Scene setup:**

- Single terrain entity at origin (or tiled for infinite look)
- Camera starts above the terrain, looking along it (like the Tron concept art)
- Adjust camera far plane if needed for larger terrain
- Keep cubes/spheres as optional debug objects (can be toggled)

**Edge colour variation (optional):** Use vertex height or position to tint the
wireframe colour — lower areas cyan, higher areas orange. Driven by a simple
gradient in the fragment shader using the vertex Y coordinate.

**After this step:** fly over a dark angular terrain with glowing wireframe edges.
**Phase 4 complete — Tron-style procedural terrain.**

### Acceptance Criteria

- [ ] Barycentric wireframe rendering in mesh + fragment shader
- [ ] Dark face colour + bright neon edge colour
- [ ] Near-black clear colour (Tron void)
- [ ] Procedural heightmap terrain generator
- [ ] Angular/terraced terrain (quantised heights)
- [ ] Terrain meshletised via buildMeshlets()
- [ ] Fly over the Tron terrain with WASD + mouse look
- [ ] Per-object frustum culling still works
- [ ] Proper doxygen, STYLE.md compliant, British spelling
- [ ] All existing + new tests pass on all CI presets
- [ ] **Phase 4 complete — Tron-style procedural terrain**

---

## Backlog

<!-- Ideas, improvements, and tasks for later phases. -->

---

## Journal

<!-- Reverse chronological — newest entries at the top. -->
<!-- Format: ### YYYY-MM-DD — Short title -->

### 2026-03-28 — Phase 3 complete: mesh shaders + scene architecture

Entity/component scene (SoA), meshlet generation, mesh shader pipeline
(VK_EXT_mesh_shader: task + mesh + fragment), mixed cube/sphere scene with
per-object meshlet routing, LOD data structures, UV sphere generator.
13 code audit fixes. Logger deadlock fixed (std::jthread). 63 PRs merged.

### 2026-03-22 — Phases 0-2.1 complete

Phase 0 (triangle), Phase 1 (fly through cubes), Phase 2 (GPU-driven, 1000 cubes,
compute frustum culling, IndirectCount, GPU timestamps), Phase 2.1 (Clang-Tidy,
spirv-val, -Werror, sanitisers, GPU validation, 4 sync bugs fixed). 59 PRs merged.
