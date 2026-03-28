# TODO & Development Journal

A shared task list and journal for humans and AI assistants working on TronGrid.

---

## Phase 3 — Mesh Shaders + Scene Architecture ✅

Completed 2026-03-28. Etapes 14 (PR #56), 15 (PR #59), 16 (PR #61), 17 (PR #63)

---

## Phase 4 — Procedural Tron Terrain ✅

Completed 2026-03-28. Etape 18 (PR #66), Etapes 19-20 (PR #68)

---

## Phase 5 — Acceleration Structures + Hard Shadows ✅

Completed 2026-03-28. Etapes 21-24 (PR #71)

---

## Phase 6 — Obsidian Floor with Neon Tube Edges

**Goal:** Make the terrain floor look like polished obsidian — a
hyperrealistic dark volcanic glass with a glossy sheen — with glowing neon
tubes running along the triangle edges. One faint overhead light casts
subtle shadows. The obsidian surface reflects the neon tubes like looking
down at a slab of polished black stone with embedded light strips.

**Scope:** Just the floor. No buildings, no sky, no objects. Get one surface
absolutely right before adding anything else.

**Why stay with ray query?** Research confirms `VK_KHR_ray_query` handles
both shadows and reflections via inline tracing in the fragment shader. The
full `VK_KHR_ray_tracing_pipeline` adds massive complexity for no benefit.
No new extensions or device features needed — the Phase 5 infrastructure
supports everything.

**Visual reference:** See `images/landscape_dark.png` — the grid floor in
the foreground: dark glass with cyan/orange neon lines, reflections of the
lines visible in the surface, soft glow halos around the lines (Phase 7).

### Etape 25 — HDR Framebuffer

Switch to an HDR intermediate render target before changing the material
model. Emissive neon tubes need values well above 1.0, and the current
SRGB swapchain clamps them.

**Approach:**

- Create an `AllocatedImage` with format `VK_FORMAT_R16G16B16A16_SFLOAT`
  at swapchain resolution. Create a matching `vk::raii::ImageView`.
- Mesh shader pass renders to the HDR image (dynamic rendering attachment)
  instead of directly to the swapchain image.
- After rendering, blit the HDR image to the swapchain image. The blit
  handles the format conversion (float16 → SRGB with clamping). Tonemapping
  comes in Phase 7 — for now the blit clamps, which is fine.
- Recreate HDR image + view on swapchain resize.
- Depth buffer stays as-is (`D32_SFLOAT`).

**Why first?** Every subsequent etape writes HDR emissive values. Without
the HDR framebuffer, those values get clamped and the neon tubes look flat
instead of blindingly bright.

**After this step:** rendering goes through an HDR intermediate. Visually
identical for now (clamped on blit), but the pipeline is ready for HDR.

### Etape 26 — Glass Material + Neon Tube Edges

Replace the flat wireframe shading with a two-material model: glass floor
tiles and emissive neon tube edges.

**Obsidian floor (triangle faces):**

- Base colour: deep black with a subtle cool tint `(0.005, 0.005, 0.01)`.
- Dielectric with `IOR ≈ 1.5` (volcanic glass / obsidian). Schlick F0 ≈ 0.04.
- Very low roughness `(0.03 - 0.08)` — polished obsidian sheen, not a
  perfect mirror. Reflections are sharp but slightly softened.
- No emissive — the floor only reflects. The obsidian surface should feel
  like a dark, heavy, glossy stone — not transparent glass.

**Neon tube edges (wireframe):**

- Emissive: bright cyan `(0.0, 0.8, 1.0) * 15.0` — HDR, intense glow.
  These are the light sources embedded in the floor.
- Base colour: black (the tube itself is not reflective, only emitting).
- Roughness: 1.0 (no specular — pure emissive glow).

**Blending:** The existing `wire` interpolant (from barycentric edge
detection) controls the blend between obsidian and neon tube. Near edges
(`wire ≈ 0`): neon tube dominates. Away from edges (`wire ≈ 1`): obsidian
surface. The `smoothstep` already provides a soft transition — this
naturally creates the "neon tube embedded in polished stone" look.

**Shader changes:** No material SSBO needed yet — the two materials can
be hardcoded constants in the shader, just organised as glass vs tube
properties instead of the current flat colour blend. The shader computes:

```text
emissive = lerp(TUBE_EMISSIVE, float3(0,0,0), wire);
base_colour = lerp(float3(0,0,0), OBSIDIAN_COLOUR, wire);
roughness = lerp(TUBE_ROUGHNESS, OBSIDIAN_ROUGHNESS, wire);
```

**Light changes:** Replace the current bright point light with a single
faint overhead light — position `(0, 50, 0)`, low intensity. The neon
tubes provide most of the scene's illumination via their emissive values.
The overhead light exists only to cast subtle shadows from terrain ridges.

**After this step:** polished obsidian tiles with bright neon tube edges.
HDR values preserved in the framebuffer. Shadow rays from the faint
overhead light still work.

### Etape 27 — PBR Lighting (Cook-Torrance)

Replace Lambertian diffuse with a physically-based BRDF for the obsidian
surface. This makes the overhead light produce correct specular highlights
on the polished floor — the glossy sheen that makes obsidian look real.

**BRDF components:**

- **Diffuse:** Lambert `(base_colour / PI)` — minimal for glass (mostly
  specular). Scaled by `(1 - F)` where F is the Fresnel term.
- **Specular:** GGX/Trowbridge-Reitz normal distribution function,
  Smith-GGX geometry function, Schlick Fresnel approximation.
- **Fresnel:** `F0 = 0.04` for dielectric obsidian. At grazing angles,
  reflectivity increases toward 1.0 — critical for the polished stone
  to look convincing. Looking straight down: faint, subtle sheen. Looking
  at a low angle: strong glossy reflection.

**CameraUBO changes:** Add `float3 camera_pos` (world-space eye position)
for the view vector `V = normalize(camera_pos - world_position)`. Add
`float camera_pad` for 16-byte alignment.

**After this step:** the obsidian floor has physically-correct specular
highlights from the overhead light. Fresnel makes the floor more
reflective at grazing angles — the hyperrealistic polished stone look
begins.

### Etape 28 — RT Reflections via Ray Query

Trace single-bounce reflection rays in the fragment shader. The polished
obsidian floor reflects the neon tube edges — the key visual payoff.

**Approach:**

- For obsidian fragments (where `wire > threshold`), compute the reflection
  direction: `reflect(view_dir, normal)`.
- Trace a `RayQuery` along the reflection direction using the same TLAS.
- Use `RAY_FLAG_CULL_BACK_FACING_TRIANGLES` (same as shadow rays).
- On hit: use `CommittedPrimitiveIndex()` + `CommittedTriangleBarycentrics()`
  to fetch vertex data from the vertex SSBO, reconstruct the hit point's
  position, compute barycentric edge detection at the hit point, evaluate
  whether the reflected point is neon tube (emissive) or glass (dark).
- The reflected colour is primarily the neon tube emissive — this creates
  bright cyan lines reflected in the dark obsidian surface.
- Blend: `colour = direct_lighting + reflected_colour * fresnel`.
  Fresnel controls reflection strength — stronger at grazing angles.
- Single bounce only — reflected rays do not recurse.

**Skip conditions (performance):**

- Skip if fragment is a neon tube edge (emissive edges glow, they do not
  reflect — they ARE the light source).
- Skip if `fresnel < 0.01` (looking straight down — reflection too faint
  to matter).

**Self-intersection:** Same approach as shadow rays — offset along normal +
`RAY_FLAG_CULL_BACK_FACING_TRIANGLES`.

**After this step:** neon tube edges reflected in the polished obsidian
floor. The hyperrealistic Tron Grid surface is complete.

### Acceptance Criteria

- [ ] HDR framebuffer (`R16G16B16A16_SFLOAT`) with blit to swapchain
- [ ] HDR image recreated on swapchain resize
- [ ] Obsidian floor material: deep black, low roughness (0.03-0.08), dielectric (F0=0.04)
- [ ] Neon tube edges: emissive HDR cyan (intensity >> 1.0)
- [ ] Smooth obsidian-to-tube transition via existing barycentric `wire` blend
- [ ] Faint overhead light (shadows still work, but light is subtle)
- [ ] Cook-Torrance BRDF: GGX specular + Schlick Fresnel
- [ ] `camera_pos` in CameraUBO for view vector and Fresnel
- [ ] Single-bounce RT reflections via inline `RayQuery` for obsidian surface
- [ ] Reflected neon tube edges visible in obsidian floor
- [ ] Fresnel-based reflection strength (grazing = strong, perpendicular = faint)
- [ ] No new extensions needed (reuses Phase 5 AS + ray query)
- [ ] Proper doxygen, STYLE.md compliant, British spelling
- [ ] All existing + new tests pass on all CI presets
- [ ] **Phase 6 complete — polished obsidian floor with neon tube reflections**

---

## Backlog

<!-- Ideas, improvements, and tasks for later phases. -->

---

## Journal

<!-- Reverse chronological — newest entries at the top. -->
<!-- Format: ### YYYY-MM-DD — Short title -->

### 2026-03-28 — Phase 5 complete: RT hard shadows

Acceleration structures (BLAS/TLAS) and inline ray query shadows via
VK_KHR_ray_query in the fragment shader (PR #71). Point light with
inverse square falloff and Lambertian diffuse. Key lessons: read vertex
normals from buffer (meshlet builder reorders winding), use
RAY_FLAG_CULL_BACK_FACING_TRIANGLES to prevent self-intersection,
light must be above max terrain height. 71 PRs merged.

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
