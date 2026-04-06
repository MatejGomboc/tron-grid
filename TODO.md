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

<details>
<summary>Phase 7 — Visual Polish ✅ (2026-04-06)</summary>

Etapes 29-36 (PRs #77-#84), shutdown fix (PR #85)

Compute post-process pipeline, ACES fitted RRT+ODT tonemapping with AP1 hue
preservation, bloom (Karis extraction + mip chain downsample + tent upsample +
HDR composite), 8× MSAA with full sample-rate shading, fwidth wireframe AA,
procedural cyberpunk skybox (value noise data fog), per-material PBR via
material SSBO, cinematic post-process (chromatic aberration, cool colour grade,
vignette, scan lines). Codebase-wide modernisation: vulkan-hpp setters,
parenthesised conditionals, Mat4::inversed(), CameraUBO inv_view_projection.

- [x] Compute post-process pipeline replaces the clamping blit
- [x] ACES Filmic tonemapping with correct sRGB gamma encoding
- [x] Orange neon appears as proper orange (not yellow — hue preserved)
- [x] Bloom extraction, downsample, upsample, and composite
- [x] Neon tubes and light orb have visible soft glow halos
- [x] Anti-aliased neon grid lines (GPU max MSAA, automatic fallback)
- [x] Procedural cyberpunk skybox (cyan-green data fog clouds)
- [x] Per-material PBR via material SSBO
- [x] Cinematic post-process (colour grade, chromatic aberration, vignette)
- [x] **Phase 7 complete — visual polish**

</details>

---

## Phase 8 — Full Ray Tracing + Advanced Rendering

**Goal:** Replace the point light abstraction with physically correct
emissive geometry lighting. Neon tubes and the orb ARE the lights —
shadow and reflection rays sample their emissive values directly. Add
multi-bounce global illumination, transparency, refraction, and
volumetric fog with neon light shafts.

### Etape 37 — Emissive Geometry as Light Sources

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

### Etape 38 — Multi-Bounce Global Illumination

Extend reflection rays to trace secondary bounces. Light that bounces
off the obsidian floor onto nearby surfaces creates subtle indirect
illumination — the hallmark of photorealistic rendering.

**Approach:**

- After the primary reflection ray hits a surface, trace a secondary
  ray from the hit point in the reflected direction.
- Evaluate the material at each bounce and accumulate colour.
- Limit to 2-3 bounces for performance.
- Russian roulette termination for unbiased path tracing.

### Etape 39 — Transparency + Refraction

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

### Etape 40 — Volumetric Fog + Light Shafts

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

### Etape 41 — Adaptive LOD + Temporal Denoising

Nanite-inspired GPU-driven level-of-detail system and temporal
accumulation for noise-free RT output at 4K @ 60+ FPS.

**Approach:**

- **Adaptive LOD** — GPU-driven meshlet streaming with automatic
  level-of-detail selection based on screen-space error. Dense meshlets
  near the camera, coarse meshlets in the distance. Seamless LOD
  transitions without popping (morphing or dithered crossfade).
- **Temporal accumulation** — reuse data from previous frames to
  denoise RT output (temporal anti-aliasing, temporal reprojection).
  Motion vectors for correct reprojection during camera movement.
- **GPU profiling** — timestamp queries for per-pass timing, automatic
  bottleneck detection. Target: 4K @ 60+ FPS sustained on RTX 4090.

**After this step:** the world renders at maximum quality with
noise-free ray tracing and automatic detail scaling.

### Acceptance Criteria

- [ ] No point light abstraction — all lighting from emissive geometry
- [ ] Shadow rays sample emissive surfaces directly
- [ ] Multi-bounce reflections (2-3 bounces)
- [ ] Russian roulette path termination
- [ ] Transparent materials with refraction (Snell's law, IOR)
- [ ] Order-independent transparency or sorted alpha
- [ ] Per-material opacity in material SSBO
- [ ] Volumetric fog with neon light shafts
- [ ] Adaptive LOD with seamless transitions
- [ ] Temporal denoising for RT output
- [ ] GPU profiling with per-pass timestamps
- [ ] 4K @ 60+ FPS sustained on RTX 4090
- [ ] Proper synchronisation barriers for all new passes
- [ ] Proper doxygen, STYLE.md compliant, British spelling
- [ ] All existing + new tests pass on all CI presets
- [ ] **Phase 8 complete — full RT + advanced rendering**

---

## Phase 9 — Engine Architecture Refactor

**Goal:** Extract the monolithic main.cpp into a proper engine structure.
Separate rendering, resource management, and scene logic into reusable
modules. Prepare the codebase for avatar integration in Phase 10.

### Planned Features

- **Engine class** — top-level coordinator that owns the Vulkan instance,
  device, swapchain, and render loop. Replaces the 2000+ line renderThread
  function with a structured pipeline.
- **Render pipeline module** — encapsulates the multi-pass rendering
  sequence (scene → skybox → bloom → tonemap → present) with explicit
  pass registration and automatic barrier management.
- **Resource manager** — centralises GPU resource creation, staging
  uploads, and lifetime management. Replaces ad-hoc buffer/image creation
  scattered across renderThread.
- **Async compute** — overlap post-processing compute with the next
  frame's mesh shader pass on separate compute queues.

### Acceptance Criteria

- [ ] Engine class with clear module boundaries
- [ ] Render pipeline extracted from renderThread
- [ ] Resource manager for GPU buffers and images
- [ ] Async compute overlap
- [ ] **Phase 9 complete — engine architecture**

---

## Phase 10 — AI Avatar Integration

**Goal:** Load AI brains as DLL/SO plugins. Avatars navigate the world,
perceive through rendered images, and interact with the environment.
See `docs/VISION.md` § Future: Multiplayer for the long-term goal.

### Planned Features

- **Avatar entity** — new entity type with position, orientation,
  velocity, identity colour. Rendered as a geometric Tron-style body.
- **AI brain plugin interface** — C-linkage DLL/SO API for perception
  (rendered image + depth), action (movement commands), and lifecycle.
- **Light trails** — moving entities leave persistent glowing streaks
  (ring buffer SSBO, ribbon geometry, emissive HDR + bloom, age fade).
- **Derez particle system** — entities dissolve into geometric particles
  (GPU compute, mesh shader point sprites, randomised velocity + fade).
- **Sensory system** — offscreen render-to-texture for bot perception,
  shared memory IPC for brain communication.

### Acceptance Criteria

- [ ] Avatar entity rendered in the world
- [ ] DLL/SO brain plugin loads and receives perception frames
- [ ] Light trails for moving entities
- [ ] Derez particle system
- [ ] Offscreen rendering for bot perception
- [ ] **Phase 10 complete — AI avatar integration**

---

## Backlog

- **Memory budget** — VMA budget tracking, streaming eviction policy,
  residency management for large scenes.
- **Multiplayer** — extract world state to authoritative server, network
  replication, multiple AI brains per instance.

---

## Journal

<!-- Reverse chronological — newest entries at the top. -->
<!-- Format: ### YYYY-MM-DD — Short title -->

### 2026-04-05 — Phase 7 Etapes 29-31: compute post-process + ACES tonemapping + bloom extraction

Fullscreen compute post-process pipeline (Etape 29, PR #77) replaces the
clamping blit with a compute shader writing directly to swapchain storage
images (B8G8R8A8_UNORM). ACES fitted RRT+ODT tonemapping with AP1
hue-preserving colour space transforms (Etape 30, PR #78) restores correct
neon colours — orange stays orange instead of clamping to yellow. Exact
IEC 61966-2-1 sRGB encoding replaces the pow(x, 1/2.2) approximation.
Bloom extraction (Etape 31, PR #79) with Karis average (firefly suppression)
and 6-level mip chain downsample via 2×2 box filter. Per-mip ImageViews and
per-step barriers. Bloom texture recreated on swapchain resize. Codebase-wide
modernisation: all Vulkan struct count+pointer assignments replaced with
vulkan-hpp setter methods, all compound conditionals parenthesised. 79 PRs
merged.

### 2026-04-06 — Phase 7 Etape 32: bloom upsample + composite

Bloom upsample chain (Etape 32, PR #80) with 3×3 tent filter and additive
blending from smallest mip back to mip 0. Post-process shader composites
bloom with HDR image before ACES tonemapping via push constant bloom_strength
(tuned to 0.4). Neon tubes and light orb now have visible soft glow halos —
the Tron aesthetic is complete. Per-mip dual barriers (source + destination)
for correct read-after-write synchronisation during additive upsample blend.
80 PRs merged.

### 2026-04-06 — Phase 7 complete: visual polish

Phase 7 complete (Etapes 29-36, PRs #77-#84). Cinematic post-process effects
(Etape 36, PR #84): chromatic aberration (RGB fringe at screen edges), cool
colour grade (cyan-blue Tron Legacy tint), vignette (corner darkening), scan
lines (faint horizontal pattern). All shader-only changes in postprocess.slang.
Phase 7 delivers: compute post-process, ACES tonemapping with AP1 hue
preservation, bloom (extract + downsample + upsample + composite), 8× MSAA
with full sample-rate shading, fwidth wireframe AA, procedural cyberpunk
skybox, per-material PBR via material SSBO, and cinematic post-process.
84 PRs merged.

### 2026-04-06 — Phase 7 Etape 35: per-material PBR via material SSBO

Per-material PBR system (Etape 35, PR #83) replaces hardcoded material
constants with a data-driven material SSBO at descriptor binding 8. Material
struct: base_colour, roughness, emissive, emissive_strength, metallic, ior,
opacity (48 bytes). ObjectData::material_type renamed to material_index.
Fragment shader loads material from SSBO array. Neon tube emissive colours
remain as shader constants (per-fragment wireframe pattern). Two materials
defined: obsidian terrain + emissive orb. 83 PRs merged.

### 2026-04-06 — Phase 7 Etape 34: procedural cyberpunk skybox

Real-time procedural skybox (Etape 34, PR #82) with value noise data fog
clouds drifting through infinite darkness. Fullscreen triangle vertex shader
(SV_VertexID, no vertex buffer), 3-octave value noise on world-space ray
direction, horizon density gradient, time-scrolled animation. MathLib extended
with Mat4::inversed() (cofactor / Cramer's rule). CameraUBO gains
inv_view_projection for skybox ray reconstruction. Renders in the same pass
as the scene (transient MSAA attachment preserved). 82 PRs merged.

### 2026-04-06 — Phase 7 Etape 33: MSAA 8× + antialiased wireframe

Hardware MSAA at GPU max sample count (8× on RTX 4090, automatic fallback
on weaker GPUs). Full sample-rate shading (minSampleShading = 1.0) evaluates
the fragment shader at every MSAA sample — critical for shader-computed
wireframe edges. Screen-space derivative wireframe antialiasing (fwidth-based
smoothstep per Wunkolo 2022) replaces fixed barycentric threshold — gives
consistent pixel-width neon lines regardless of triangle size or camera
distance. Transient MSAA + depth images with lazily-allocated memory hint.
Dynamic rendering resolve (eAverage) into single-sample HDR. Cross-frame
MSAA barrier synchronisation. Bloom strength tuned to 0.25. 81 PRs merged.

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
