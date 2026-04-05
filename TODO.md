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
1. Render scene → MSAA HDR image (R16G16B16A16_SFLOAT, 4× samples)
2. Resolve MSAA → single-sample HDR image
3. Bloom extraction → bright pixels above threshold into bloom texture
4. Bloom blur → multi-pass Gaussian downsample/upsample (mip chain)
5. Composite → HDR + bloom → tonemapped → swapchain (sRGB)
```

### Etape 29 — Fullscreen Compute Pass Infrastructure

Set up the compute pipeline infrastructure for post-processing. This
etape does not change the visual output — it replaces the blit with a
compute pass that copies the HDR image to the swapchain (identity
tonemapping), proving the compute pipeline works.

**Approach:**

- Write a Slang compute shader (`postprocess.slang`) that reads from
  the HDR image and writes to the swapchain image. Entry point
  `postprocessMain`, workgroup size 8×8. Uses `imageLoad()`/`imageStore()`
  with `gl_GlobalInvocationID.xy` as pixel coordinates.
- Descriptor set layout: 1 storage image (HDR input, `rgba16f`,
  `readonly`) + 1 storage image (swapchain output, `rgba8`, `writeonly`).
- Create a `VkComputePipeline` with `VK_PIPELINE_BIND_POINT_COMPUTE`.
  No render pass, no viewport — just bind pipeline, bind descriptors,
  dispatch.
- Replace the `vkCmdBlitImage2` call in `recordFrame` with:
  1. Transition HDR: `eColorAttachmentOptimal` → `eGeneral` (compute read).
  2. Transition swapchain: `eUndefined` → `eGeneral` (compute write).
  3. Bind compute pipeline + descriptors.
  4. `vkCmdDispatch(ceil(width/8), ceil(height/8), 1)`.
  5. Transition swapchain: `eGeneral` → `ePresentSrcKHR`.
- The compute shader initially performs a simple copy with clamping:
  `output[coord] = clamp(input[coord], 0, 1)`. This verifies the
  pipeline, barriers, and descriptor bindings before adding tonemapping.
- Swapchain image usage must include `VK_IMAGE_USAGE_STORAGE_BIT`
  (verify via `VkSurfaceCapabilitiesKHR::supportedUsageFlags`).

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
  and writes pixels above a brightness threshold to a separate bloom
  texture at half resolution. Pixels below threshold are black.
  Brightness is computed as weighted luminance:
  `dot(colour.rgb, float3(0.2126, 0.7152, 0.0722))`. Threshold at
  `luminance > 1.0` — only HDR-bright pixels contribute to bloom.
- **Downsample chain:** Create a mip chain (e.g., 6 levels) on the
  bloom texture. Each level is half the resolution of the previous.
  A compute pass downsamples each level with a 13-tap filter (the
  Karis average for the first downsample to prevent fireflies from
  sub-pixel bright spots). Each mip level needs its own `ImageView`
  for the compute shader to write to. Note: multisampled images
  cannot have mip chains — resolve MSAA before bloom.
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

### Etape 33 — Anti-Aliasing (MSAA or FXAA)

The neon grid lines suffer from aliasing — jagged stair-stepping that
breaks the clean geometric aesthetic. Anti-aliasing smooths the edges.

**Candidates:**

- **MSAA 4×** — hardware multi-sample anti-aliasing. Resolve the HDR
  colour attachment from a 4× MSAA image to the single-sample HDR image
  before the post-process pass. Requires creating the HDR image with
  `VK_SAMPLE_COUNT_4_BIT` and a separate single-sample resolve target.
  Best quality for geometric edges (exactly what wireframe neon needs).
  Cost: 4× fragment shading + resolve bandwidth.
- **FXAA** — fast approximate anti-aliasing as a post-process compute
  pass after tonemapping. Cheaper than MSAA but blurs textures. Since
  we have no textures (pure procedural wireframe + emissive), FXAA
  would work well and is simpler to implement (single compute pass on
  the final sRGB image).

**Recommendation:** Start with MSAA 4× for the best wireframe edge
quality. The neon tubes are thin sub-pixel lines — MSAA handles these
much better than post-process AA. If performance is an issue, fall back
to FXAA.

**Approach (MSAA 4×):**

- Query `VkPhysicalDeviceLimits::framebufferColorSampleCounts &
  framebufferDepthSampleCounts` and select the highest supported sample
  count (64×, 32×, 16×, 8×, 4×, 2×). Use the GPU's maximum capability
  by default — the RTX 4090 supports 8× and higher. Store the selected
  count in `Device` (e.g., `m_max_msaa_samples`). Log it at startup.
  On weaker GPUs, the same code path automatically falls back to
  whatever the hardware supports — no manual override needed.
- Create the HDR colour image with `VK_SAMPLE_COUNT_4_BIT` and
  `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | COLOR_ATTACHMENT_BIT`.
  Multisampled images must have exactly 1 mip level.
- Create a single-sample HDR resolve target (the existing HDR image
  becomes this). The MSAA image is a new, separate resource.
- The depth buffer also needs `VK_SAMPLE_COUNT_4_BIT` to match.
- The mesh shader pipeline's `VkPipelineMultisampleStateCreateInfo`
  must set `rasterizationSamples = VK_SAMPLE_COUNT_4_BIT`.
- Dynamic rendering: set the MSAA image as the colour attachment and
  the single-sample HDR image as the resolve attachment (`resolveMode =
  VK_RESOLVE_MODE_AVERAGE_BIT`). The resolve happens automatically at
  the end of rendering — no separate `vkCmdResolveImage2` needed.
- The post-process (bloom + tonemap) reads from the resolved
  single-sample image as before.
- Recreate both MSAA + resolve images on swapchain resize.
- Optional: enable `sampleRateShading` with `minSampleShading = 0.25`
  for even better quality on thin sub-pixel neon lines (trades
  performance for smoother anti-aliasing).

**After this step:** neon grid lines are smooth and clean, no jagged
stair-stepping. The Tron aesthetic is polished.

### Etape 34 — Procedural Cyberpunk Skybox

The sky should feel like a digital atmosphere — not outer space with
stars, but faintly glowing greenish-cyan clouds drifting through
infinite darkness. Think data fog, digital aurora, cyberpunk haze.
The clouds are dim enough to not compete with the neon grid but
bright enough to break the flat black void and add depth.

**Approach:**

- Write a fullscreen fragment shader (`skybox.slang`) that runs as a
  separate pass after the scene render, using the depth buffer to skip
  fragments covered by geometry (depth test, no depth write).
- The shader takes the inverse view-projection matrix to reconstruct
  the world-space ray direction from the fragment's screen position.
- **Cloud layer:** Layered value noise (3-4 octaves) on the ray
  direction to create slow-rolling volumetric cloud shapes. Colour:
  dark greenish-cyan tint `(0.0, 0.08, 0.06)` with brighter wisps
  at `(0.0, 0.15, 0.12)`. The clouds are faint — mostly darkness
  with subtle luminous patches.
- **Depth gradient:** Clouds near the horizon are denser and brighter;
  clouds overhead are sparser. Use `1.0 - abs(ray_dir.y)` as a
  density multiplier to concentrate the haze at the horizon line
  where it frames the terrain silhouette.
- **Animation:** Slowly scroll the noise coordinates over time for
  drifting cloud motion. Very slow — the clouds should feel like a
  distant atmospheric phenomenon, not weather.
- No geometry needed — the skybox is purely procedural in the
  fragment shader, rendering to the HDR image.

**After this step:** the sky is a dark digital atmosphere with faintly
glowing cyan-green cloud wisps — cyberpunk, not space opera.

### Etape 35 — Per-Material PBR (Material SSBO)

Replace the hardcoded material constants in the fragment shader with
a per-object material system. Each entity gets its own PBR parameters
from a material SSBO.

**Approach:**

- Define a `Material` struct (C++ and Slang):

  ```text
  base_colour (float3)     — albedo / diffuse colour
  emissive (float3)        — self-illumination colour
  emissive_strength (float) — HDR emissive multiplier
  roughness (float)        — perceptual roughness [0, 1]
  metallic (float)         — metalness [0, 1]
  ior (float)              — index of refraction (for Fresnel F0)
  opacity (float)          — 1.0 = opaque, <1.0 = translucent (Phase 8)
  ```

- Create a material SSBO and bind it at a new descriptor binding.
- `ObjectData` gains a `material_index` field pointing into the SSBO.
- The fragment shader reads the material for the current object and
  uses it instead of hardcoded constants. The neon tube vs obsidian
  blend logic stays (it's per-fragment, not per-object), but the
  material properties it blends between come from the SSBO.
- The light orb's emissive colour and strength are now data-driven.

**After this step:** materials are data, not code. New objects with
different visual properties can be added without shader changes.

### Etape 36 — Cinematic Post-Process Effects

Add film-like post-processing to sell the "inside a digital world"
aesthetic. Inspired by Tron Legacy's colour grading: warm colours
almost completely removed in favour of cool monochromatic tints.

**Effects (all in the post-process compute shader):**

- **Cool colour grade** — shift the tonemapped image toward cool
  cyan-blue tints, suppress warm colours. A simple colour matrix
  multiply or per-channel curve. Configurable intensity.
- **Chromatic aberration** — subtle RGB fringe at screen edges.
  Sample the image at slightly offset UVs per channel. Very cheap,
  very cyberpunk.
- **Vignette** — darken screen corners to draw the eye inward and
  frame the neon grid. A radial falloff from screen centre.
- **Scan lines / CRT noise** — faint horizontal line pattern and
  subtle noise overlay. Sells the "digital display" aesthetic.
  Optional / toggleable for taste.

**After this step:** the image looks like it was filmed inside the
Grid, not rendered by a computer. Cool tints, edge distortion, and
subtle digital noise complete the Tron Legacy look.

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
- [ ] Anti-aliased neon grid lines (GPU max MSAA, automatic fallback)
- [ ] AA resources recreated on swapchain resize
- [ ] Procedural cyberpunk skybox (cyan-green data fog clouds)
- [ ] Per-material PBR via material SSBO
- [ ] Cinematic post-process (colour grade, chromatic aberration, vignette)
- [ ] No new Vulkan extensions needed (compute + MSAA are core 1.0)
- [ ] Proper synchronisation barriers for all compute passes
- [ ] Proper doxygen, STYLE.md compliant, British spelling
- [ ] All existing + new tests pass on all CI presets
- [ ] **Phase 7 complete — visual polish**

---

## Phase 8 — Full Ray Tracing + Advanced Rendering

**Goal:** Replace the point light abstraction with physically correct
emissive geometry lighting. Neon tubes and the orb ARE the lights —
shadow and reflection rays sample their emissive values directly. Add
multi-bounce global illumination, transparency, and refraction.

### Etape 36 — Emissive Geometry as Light Sources

Remove the artificial point light. Instead, shadow rays that hit
emissive geometry (neon tubes, orb) contribute their emissive value
as incoming radiance. The fragment shader's direct lighting loop
becomes: for each light-emitting surface, trace a ray toward it,
evaluate visibility, accumulate radiance weighted by the BRDF.

**Approach:**

- Sample random points on emissive geometry (importance sampling).
- Trace shadow rays toward those sample points.
- On hit: evaluate the emissive material at the hit point.
- Weight by the BRDF, solid angle, and visibility.
- Multiple samples per pixel (stratified) for noise reduction.

### Etape 37 — Multi-Bounce Global Illumination

Extend reflection rays to trace secondary bounces. Light that bounces
off the obsidian floor onto nearby surfaces creates subtle indirect
illumination — the hallmark of photorealistic rendering.

**Approach:**

- After the primary reflection ray hits a surface, trace a secondary
  ray from the hit point in the reflected direction.
- Evaluate the material at each bounce and accumulate colour.
- Limit to 2-3 bounces for performance.
- Russian roulette termination for unbiased path tracing.

### Etape 38 — Transparency + Refraction

Add support for translucent materials (glass, energy barriers, holographic
displays). Refraction rays bend through surfaces based on the material's
index of refraction.

**Approach:**

- Materials with `opacity < 1.0` trace refraction rays via Snell's law.
- Total internal reflection handled at critical angles.
- Order-independent transparency (OIT) or sorted alpha blending for
  correct compositing of overlapping translucent surfaces.
- Refraction uses the same inline ray query (`VK_KHR_ray_query`) —
  no new extensions needed.

### Etape 39 — Volumetric Fog + Light Shafts

Neon light scattering through atmospheric haze — the #1 mood tool in
cyberpunk rendering (Cyberpunk 2077 uses volumetric fog extensively).
Inspired by Tron Legacy where scenes were lit from below and "light
sprang from within the world."

**Approach:**

- Compute shader raymarches through a 3D volume texture (froxel grid)
  to accumulate in-scattered light from emissive surfaces.
- The fog density is low (faint haze) but concentrates near the ground
  where the neon tubes emit — creating visible light shafts rising from
  the grid lines.
- Temporal reprojection reuses previous frame data to reduce noise.
- The fog colour picks up the emissive colour of nearby neon tubes
  (cyan shafts from cyan lines, orange from orange lines).

### Etape 40 — Light Trails

Moving objects leave persistent glowing streaks — the signature Tron
visual. Core identity for light cycles, data couriers, and any
moving programme.

**Approach:**

- A trail buffer (SSBO) stores a ring buffer of past positions per
  entity. Each frame, the current position is appended.
- A compute or mesh shader reads the trail buffer and generates thin
  ribbon geometry connecting past positions.
- The ribbon uses the same emissive material as neon tubes (HDR,
  bloomed). Trail colour matches the entity's identity colour.
- Trails fade over time (age-based alpha/emissive decay).

### Etape 41 — Derez Particle System

Entities dissolve into geometric particles when destroyed — the Tron
"derez" effect. Digital Domain used procedural explosion algorithms
(the "Egyptian algorithm") to generate travelling linework for derez
sequences.

**Approach:**

- GPU compute particle system — particles are stored in an SSBO,
  updated by a compute shader each frame (position, velocity, lifetime).
- On derez trigger: the entity's mesh vertices become particle spawn
  positions. Each particle inherits the vertex's emissive colour.
- Particles fly outward with randomised velocity, shrink, and fade.
- Rendered as point sprites or tiny billboard quads via mesh shader.
- The particle system is general-purpose — also used for energy sparks,
  data stream effects, and environmental ambience.

### Acceptance Criteria

- [ ] No point light abstraction — all lighting from emissive geometry
- [ ] Shadow rays sample emissive surfaces directly
- [ ] Multi-bounce reflections (2-3 bounces)
- [ ] Russian roulette path termination
- [ ] Transparent materials with refraction (Snell's law, IOR)
- [ ] Order-independent transparency or sorted alpha
- [ ] Per-material opacity in material SSBO
- [ ] Volumetric fog with neon light shafts
- [ ] Light trails for moving entities (ring buffer + ribbon geometry)
- [ ] Derez particle system (GPU compute, mesh shader rendered)
- [ ] **Phase 8 complete — full RT + Tron effects**

---

## Phase 9 — Optimisation

**Goal:** Hit 4K @ 60+ FPS rock-solid on RTX 4090. Adaptive quality
scaling for weaker hardware.

### Planned Features

- **Nanite-like adaptive LOD** — GPU-driven mesh streaming with
  automatic level-of-detail selection. Dense meshlets near the camera,
  coarse meshlets in the distance. Software rasterisation for sub-pixel
  triangles. Seamless LOD transitions without popping.
- **Temporal accumulation** — reuse data from previous frames to
  denoise RT output (temporal anti-aliasing, temporal reprojection).
- **Async compute** — overlap post-processing compute with the next
  frame's mesh shader pass on separate compute queues.
- **GPU profiling** — timestamp queries for per-pass timing, automatic
  bottleneck detection, adaptive quality scaling.
- **Memory budget** — VMA budget tracking, streaming eviction policy,
  residency management for large scenes.

### Acceptance Criteria

- [ ] 4K @ 60+ FPS sustained on RTX 4090
- [ ] Adaptive LOD with seamless transitions
- [ ] Temporal denoising for RT output
- [ ] Async compute overlap
- [ ] GPU profiling with per-pass timestamps
- [ ] **Phase 9 complete — optimisation**

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
