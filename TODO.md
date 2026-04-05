# TODO & Development Journal

A shared task list and journal for humans and AI assistants working on TronGrid.

---

## Completed Phases

<details>
<summary>Phase 3 — Mesh Shaders + Scene Architecture ✅ (2026-03-28)</summary>

Etapes 14 (PR #56), 15 (PR #59), 16 (PR #61), 17 (PR #63)

</details>

<details>
<summary>Phase 4 — Procedural Tron Terrain ✅ (2026-03-28)</summary>

Etape 18 (PR #66), Etapes 19-20 (PR #68)

</details>

<details>
<summary>Phase 5 — Acceleration Structures + Hard Shadows ✅ (2026-03-28)</summary>

Etapes 21-24 (PR #71)

</details>

<details>
<summary>Phase 6 — PBR Obsidian Floor with Neon Tube Reflections ✅ (2026-04-05)</summary>

Etapes 25-28 (PR #74), orange accent + light orb + 5 code audits (PR #75)

- [x] HDR framebuffer (`R16G16B16A16_SFLOAT`) with blit to swapchain
- [x] HDR image recreated on swapchain resize
- [x] Obsidian floor material: deep black, low roughness (0.06), dielectric (F0=0.04)
- [x] Neon tube edges: emissive HDR cyan + orange dual-colour palette (intensity 15.0)
- [x] Orange accent on major grid lines (every 8th row/column)
- [x] Smooth obsidian-to-tube transition via existing barycentric `wire` blend
- [x] Faint overhead light (shadows still work, but light is subtle)
- [x] Cook-Torrance BRDF: GGX specular + Schlick Fresnel
- [x] `camera_pos` in CameraUBO for view vector and Fresnel
- [x] Single-bounce RT reflections via inline `RayQuery` for obsidian surface
- [x] Reflected neon tube edges visible in obsidian floor (with correct dual-colour)
- [x] Fresnel-based reflection strength (grazing = strong, perpendicular = faint)
- [x] Emissive orange light orb sphere above the grid
- [x] Quad diagonal edge suppression (cleaner rectangular grid)
- [x] Two-sided lighting for steep terrain slopes
- [x] 5 code audits: 47 fixes (bugs, correctness, style, docs, CMake)
- [x] **Phase 6 complete — polished obsidian floor with neon tube reflections**

</details>

---

## Phase 7 — Post-Processing (Bloom + Tonemapping)

**Goal:** Make the neon tubes and emissive orb *glow* with soft halos that
bleed into surrounding pixels, and replace the HDR clamping blit with
proper tonemapping. This is the single biggest visual upgrade remaining —
the difference between "coloured wireframe" and "blindingly bright neon
against infinite darkness."

**Visual reference:** See `images/landscape_dark.png` — every neon line
and emissive element has a thick, soft glow halo. The orange ring in the
sky bleeds light across the frame. This is bloom.

**Current pipeline (Phase 6):**

```text
1. Render scene → HDR image (R16G16B16A16_SFLOAT)
2. Blit HDR → swapchain (clamping float16 → sRGB, losing all HDR data)
```

**Target pipeline (Phase 7):**

```text
1. Render scene → HDR image (R16G16B16A16_SFLOAT)
2. Bloom extraction → bright pixels above threshold into bloom texture
3. Bloom blur → multi-pass Gaussian downsample/upsample (mip chain)
4. Composite → HDR + bloom → tonemapped → swapchain (sRGB)
```

### Etape 29 — Fullscreen Compute Pass Infrastructure

Set up the compute pipeline infrastructure for post-processing. This
etape does not change the visual output — it replaces the blit with a
compute pass that copies the HDR image to the swapchain (identity
tonemapping), proving the compute pipeline works.

**Approach:**

- Write a Slang compute shader (`postprocess.slang`) that reads from
  the HDR image (sampled or storage) and writes to the swapchain image.
  Entry point `postprocessMain`, workgroup size 8×8.
- Create a compute pipeline with a descriptor set layout: 1 sampled
  image (HDR input) + 1 storage image (swapchain output).
- Replace the `vkCmdBlitImage2` call in `recordFrame` with a compute
  dispatch: transition swapchain to `eGeneral`, dispatch workgroups
  covering the full resolution, transition swapchain to `ePresentSrcKHR`.
- The compute shader initially performs a simple copy (identity):
  `output[coord] = input[coord]`. This verifies the pipeline, barriers,
  and descriptor bindings before adding tonemapping.

**After this step:** rendering goes through a compute post-process pass.
Visually identical to the current blit (still clamping), but the
infrastructure is in place for bloom and tonemapping.

### Etape 30 — ACES Filmic Tonemapping

Add tonemapping to the compute shader so HDR values are compressed into
the displayable [0, 1] range with proper contrast and hue preservation.

**Approach:**

- Implement ACES Filmic tonemapping in `postprocess.slang`:

  ```text
  colour = (colour * (2.51 * colour + 0.03)) / (colour * (2.43 * colour + 0.59) + 0.14)
  ```

- Apply sRGB gamma encoding after tonemapping (the swapchain image is
  `B8G8R8A8_SRGB`, but when written as a storage image the hardware
  does not apply gamma — the shader must do `pow(colour, 1/2.2)` or
  use the linear-to-sRGB transfer function).
- The orange neon and orb will now appear as proper orange instead of
  yellow — tonemapping preserves hue ratios that clamping destroys.
- The cyan neon will gain depth — bright cores with soft rolloff
  instead of flat white.

**After this step:** the scene has cinematic contrast. Bright emissive
values are compressed gracefully instead of clipped. The orange neon
colour is restored.

### Etape 31 — Bloom Extraction + Downsample Chain

Extract bright pixels from the HDR image and create a Gaussian blur
mip chain for the bloom halo.

**Approach:**

- **Bloom threshold extraction:** a compute pass reads the HDR image
  and writes pixels above a brightness threshold (e.g.,
  `luminance > 1.0`) to a separate bloom texture at half resolution.
  Pixels below threshold are black.
- **Downsample chain:** Create a mip chain (e.g., 6 levels) on the
  bloom texture. Each level is half the resolution of the previous.
  A compute pass downsamples each level with a 13-tap filter (the
  Karis average for the first downsample to prevent fireflies from
  sub-pixel bright spots).
- The bloom texture uses `R16G16B16A16_SFLOAT` (same as HDR) to
  preserve colour fidelity through the blur chain.
- Recreate the bloom texture and its mip chain on swapchain resize.

**After this step:** a blurred bloom texture exists with soft halos
around all bright emissive elements. Not yet composited — the final
image is still tonemapped-only.

### Etape 32 — Bloom Upsample + Composite

Upsample the bloom chain and composite it with the HDR image before
tonemapping.

**Approach:**

- **Upsample chain:** Starting from the smallest mip level, upsample
  each level and additively blend with the next larger level. Use
  bilinear filtering. This produces a smooth, wide bloom halo from
  the sharp bright spots.
- **Composite:** In the tonemapping compute shader, add the final
  bloom value to the HDR colour before tonemapping:

  ```text
  colour = hdr_colour + bloom_strength * bloom_colour
  tonemap(colour)
  ```

- `bloom_strength` is a tunable constant (start with 0.5).
- The bloom adds the glow halos around neon tubes and the light orb
  that the concept art shows. The Tron aesthetic is complete.

**After this step:** neon lines glow with soft halos, the light orb
radiates warmth, and the scene matches the concept art aesthetic.

### Acceptance Criteria

- [ ] Compute post-process pipeline replaces the clamping blit
- [ ] ACES Filmic tonemapping with correct sRGB gamma encoding
- [ ] Orange neon appears as proper orange (not yellow — hue preserved)
- [ ] Bloom extraction with brightness threshold
- [ ] Bloom downsample mip chain (Karis average for first level)
- [ ] Bloom upsample chain with additive blending
- [ ] Bloom composite with tunable strength
- [ ] Bloom texture recreated on swapchain resize
- [ ] Neon tubes and light orb have visible soft glow halos
- [ ] No new Vulkan extensions needed (compute is core 1.0)
- [ ] Proper synchronisation barriers for all compute passes
- [ ] Proper doxygen, STYLE.md compliant, British spelling
- [ ] All existing + new tests pass on all CI presets
- [ ] **Phase 7 complete — bloom + tonemapping post-processing**

---

## Backlog

<!-- Ideas, improvements, and tasks for later phases. -->

---

## Journal

<!-- Reverse chronological — newest entries at the top. -->
<!-- Format: ### YYYY-MM-DD — Short title -->

### 2026-04-05 — Phase 6 complete: PBR obsidian floor with neon tube reflections

HDR framebuffer (R16G16B16A16_SFLOAT → blit to sRGB swapchain),
Cook-Torrance BRDF (GGX NDF, height-correlated Smith-GGX, Schlick
Fresnel), obsidian material (F0=0.04, roughness 0.06), emissive neon tube
edges with dual-colour palette (cyan primary + orange accent on major grid
lines every 8 cells), single-bounce RT reflections via inline ray query.
Reflections correctly evaluate the dual-colour pattern at the hit point.
Emissive orange light orb sphere at the point light position. Quad
diagonal edge suppression for cleaner rectangular wireframe. Five code
audits found and fixed 47 issues (fence deadlock, semaphore spec
violations, signed overflow UB, Linux mouse look, AS scratch alignment,
CMake minimum version, spirv-val target env, and more). Key lessons:
Filament's numerically stable NDF form prevents float16 cancellation,
HDR clamping destroys hue ratios (orange → yellow), tonemapping in
Phase 7 will restore correct colours. 76 PRs merged.

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
