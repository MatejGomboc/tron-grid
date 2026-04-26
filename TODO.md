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

**Goal:** Render the non-living Tron Grid world at Unreal Engine quality.
Replace the point light abstraction with physically correct emissive
geometry lighting using ReSTIR. Add multi-bounce GI, transparency,
volumetric fog, adaptive LOD, and temporal denoising.

### Etape 37 — Emissive Geometry as Light Sources (ReSTIR DI)

Remove the artificial point light. Neon tubes and the orb ARE the lights
— shadow rays that hit emissive geometry contribute their emissive value
as incoming radiance. Use ReSTIR DI (Reservoir-based Spatiotemporal
Importance Resampling for Direct Illumination) to efficiently sample
many emissive surfaces with just 1-2 rays per pixel.

**Approach:**

- **ReSTIR DI** (NVIDIA 2020) — reservoir-based sampling in 3 passes:
  initialisation (RIS candidate sampling), temporal reuse (merge prior
  frame's reservoir), spatial reuse (merge neighbouring pixels).
- Shadow rays toward emissive geometry sample points.
- BRDF-weighted radiance accumulation with visibility checks.
- **Motion vectors** generated during the geometry pass (per-pixel
  screen-space velocity). Essential for temporal reuse in ReSTIR,
  temporal denoising (Etape 41), and volumetric fog reprojection
  (Etape 40). Written to a dedicated R16G16 render target.

**After this step:** all lighting comes from actual emissive surfaces
— no fake point light. The scene has physically correct direct lighting.

### Etape 38 — Multi-Bounce Global Illumination (ReSTIR GI)

Extend to multi-bounce indirect illumination. Light bouncing off the
obsidian floor onto surfaces creates subtle colour bleeding — the
hallmark of photorealistic rendering.

**Approach:**

- **ReSTIR GI** (NVIDIA 2021) — path resampling for indirect lighting.
  Reuses paths across space and time for noise-free GI with few samples.
- **World-space irradiance cache** (inspired by UE5 Lumen's surface
  cache) — amortise GI computation across frames by caching lighting
  on surfaces. Update incrementally, not every frame.
- 2-3 bounce limit with Russian roulette termination.
- Indirect specular (glossy reflections) via the same path tracing.

**After this step:** the obsidian floor glows faintly from neon tube
light, colour bleeds between surfaces, the world has photorealistic
indirect illumination.

### Etape 39 — Ray-Traced Ambient Occlusion (RTAO)

Add physically accurate ambient occlusion via short-range ray tracing.
Soft contact shadows in corners and crevices where geometry meets —
the obsidian floor at terrain edges, data tower bases, under barriers.

**Approach:**

- **RTAO** via inline ray query — trace short hemispherical rays from
  each surface point. Count occlusion ratio. 1 ray per pixel with
  temporal accumulation + spatial filtering for noise-free result.
- Reuses the existing BLAS/TLAS — no additional acceleration structures.
- Applied before lighting in the shading pipeline (multiplies diffuse).
- Much more accurate than SSAO/GTAO — no screen-space artefacts,
  works correctly in corners, under overhangs, and at silhouettes.

**After this step:** subtle darkening in corners and crevices adds
depth and grounding to every surface.

### Etape 40 — Transparency + Refraction

Add translucent materials: glass, energy barriers, holographic displays.
Refraction rays bend through surfaces based on index of refraction.

**Approach:**

- **Weighted Blended OIT** (McGuire & Bavoil 2013) for rasterised
  transparency — fast, simple, works with standard rasterisation.
- **Ray-traced refraction** via Snell's law using inline ray query
  for high-quality glass and holographic surfaces.
- Total internal reflection at critical angles.
- Per-material `opacity` field already in Material SSBO.
- No new Vulkan extensions needed.

**After this step:** energy barriers shimmer with refraction, holographic
displays distort the scene behind them.

### Etape 41 — Volumetric Fog + Light Shafts

Neon light scattering through atmospheric haze — the #1 mood tool in
cyberpunk rendering (Frostbite/DICE technique).

**Approach:**

- **Froxel-based** (frustum-aligned voxels) volumetric fog — the AAA
  standard used by Frostbite, UE4/5, and Unity HDRP.
- Compute shader injects light and density into a 3D froxel grid.
- Raymarch through the grid per pixel to accumulate in-scattered light.
- Fog density concentrates near the ground where neon tubes emit —
  creating visible coloured light shafts (cyan from cyan lines, orange
  from orange lines).
- **Temporal reprojection** reuses previous frame data to reduce noise.

**After this step:** faint neon-coloured fog rises from the grid lines,
light shafts pierce the darkness — the world breathes.

### Etape 42 — Adaptive LOD + Temporal Denoising

Nanite-inspired GPU-driven level-of-detail and temporal accumulation
for noise-free RT at 4K @ 60+ FPS.

**Approach:**

- **Adaptive meshlet LOD** — hierarchical meshlet DAG with GPU-driven
  selection based on screen-space error (Nanite technique). Dense
  meshlets near camera, coarse in the distance. Seamless transitions
  via morphing or dithered crossfade.
- **Temporal accumulation** — reuse prior frames to denoise RT output.
  Motion vectors (from Etape 37) enable correct reprojection during
  camera movement. Exponential moving average with rejection for
  disoccluded pixels.
- **GPU profiling** — timestamp queries for per-pass timing, automatic
  bottleneck detection. Target: 4K @ 60+ FPS sustained on RTX 4090.

**After this step:** the world renders at maximum quality with noise-free
ray tracing and automatic detail scaling. Unreal Engine-quality output.

### Acceptance Criteria

- [x] No point light abstraction — all lighting from emissive geometry
- [x] ReSTIR DI for direct lighting from emissive surfaces
- [x] Single-bounce indirect GI via cosine-weighted hemisphere sampling (Etape 38)
- [ ] Full ReSTIR GI with path resampling and multi-bounce indirect
- [ ] World-space irradiance cache
- [x] Russian roulette path termination
- [x] Motion vectors for temporal reuse
- [x] Ray-traced ambient occlusion (RTAO)
- [x] Transparent materials with refraction (Snell's law, IOR)
- [ ] Weighted Blended OIT or equivalent (Etape 40 deliberately uses simpler premultiplied-alpha
  blending — full WBOIT is deferred until the scene has enough overlapping transparents to
  justify the accumulation/revealage targets)
- [x] Per-material opacity in material SSBO
- [ ] Froxel-based volumetric fog with neon light shafts
- [ ] Temporal reprojection for fog noise reduction
- [ ] Adaptive meshlet LOD with seamless transitions
- [ ] Temporal denoising for RT output
- [ ] GPU profiling with per-pass timestamps
- [ ] 4K @ 60+ FPS sustained on RTX 4090
- [ ] Proper synchronisation barriers for all new passes
- [ ] Proper doxygen, STYLE.md compliant, British spelling
- [ ] All existing + new tests pass on all CI presets
- [ ] **Phase 8 complete — full RT + advanced rendering**

---

## Library Hardening (Maintenance — runs in parallel with phases)

A parallel `libs/` audit (2026-04-26) surfaced ten findings ranging from
correctness bugs to cosmetic issues. None block any in-progress phase work,
so they are scheduled as a sequence of small focused PRs that can land
between or alongside main-phase etapes. Each maintenance etape is one PR
on its own branch off `main`.

### Maintenance Etape M1 — logger race + math/quaternion hardening

**Goal:** fix the most consequential cross-cutting bugs in
`libs/logging` and `libs/math`. These are small files, the changes are
local, and they unblock confidence in any future investigation that
relies on logging or matrix data layout.

**Scope:**

- [ ] **Logger missed-wakeup race** (`libs/logging/src/logger.cpp:82-86`).
  `enqueue` calls `m_queue.emit()` outside `m_mutex`, then `notify_one()`.
  The waiter at `:93-96` reads `m_queue.empty()` under `m_mutex`. Per the
  C++ memory model, state read by the predicate must be modified under
  the same mutex used by `wait` to avoid lost wake-ups. Fix: hold
  `m_mutex` while calling `m_queue.emit(...)` in `enqueue`. (Alternative:
  drop the redundant CV/mutex and use a `Signal` wait directly — bigger
  change, defer.)
- [ ] **Missing `<algorithm>` in quaternion**
  (`libs/math/include/math/quaternion.hpp:148`). Uses `std::min` but only
  includes `<cmath>` / `<array>`. Compiles by transitive include today —
  add `#include <algorithm>` to make it standard-conformant.
- [ ] **`Mat4::data()` nested-array UB**
  (`libs/math/include/math/matrix.hpp:179-182`). Returns `&m[0][0]` on a
  `std::array<std::array<float, 4>, 4>`; standard does not guarantee
  contiguity across inner arrays. Switch storage to `std::array<float, 16>`
  with `(col * 4 + row)` accessors. Update `math_tests.cpp:264-267` to
  match.
- [ ] **Logger destructor redundant `notify_one()`** (`libs/logging/src/logger.cpp:53`).
  The `std::stop_token`-aware `cv.wait` overload already wakes on stop;
  the manual notify is harmless noise. Remove.
- [ ] **`testing` library `toString` opaqueness** (`libs/testing/include/testing/testing.hpp:64`).
  Returns `"<?>"` for non-arithmetic non-string types — failure messages
  for `Vec3`, `Mat4` etc. read as `<?> != <?>`. Add an ADL-detected
  `to_string(v)` hook so project types print useful diagnostics.

**Acceptance:** all `libs/` tests pass on every preset; new tests cover
the logger race fix (synthetic producer + thread-sanitiser preset) and
the new `Mat4::data()` layout invariant.

### Maintenance Etape M2 — Win32 window hardening

**Goal:** clean up the Windows window-backend bugs that affect cursor
behaviour, system keys, and the visible "hollow" startup window.

**Scope:**

- [ ] **`setCursorCaptured` unbalanced `ShowCursor` counter**
  (`libs/window/src/win32_window.cpp:121-142`). `ShowCursor` keeps a
  per-process counter; double-call to `setCursorCaptured(true)` sinks
  it to -2 and a single `(false)` only restores to -1, leaving the
  cursor hidden. Add an early return when `m_cursor_captured == captured`.
- [ ] **`WM_SYSKEYDOWN` / `WM_SYSKEYUP` swallow system keys**
  (`libs/window/src/win32_window.cpp:212-228`). Both handlers `return 0`
  instead of falling through to `DefWindowProcW`. Breaks Alt+F4,
  Alt+Space (system menu), F10. Forward to `DefWindowProcW` from
  `WM_SYSKEY*`, or synthesise a Close event for Alt+F4 explicitly.
- [ ] **Hollow window before first Vulkan frame**
  (`libs/window/src/win32_window.cpp` window-class registration). Caused
  by `WS_EX_NOREDIRECTIONBITMAP` excluding the window from DWM
  compositing combined with no GDI background brush. Fix: remove
  `WS_EX_NOREDIRECTIONBITMAP` and set
  `wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH)` so the client
  area paints black before the swapchain present.
- [ ] **Title string off-by-one** (`libs/window/src/win32_window.cpp:72-74`).
  `MultiByteToWideChar` with `cchSrc = -1` returns the count *including*
  the trailing null; sizing the `std::wstring` with that count puts a
  spurious `L'\0'` at `back()`. Use `title_len - 1`.
- [ ] **`CS_OWNDC` cosmetic** (`libs/window/src/win32_window.cpp:34`).
  Meaningless when paired with `WS_EX_NOREDIRECTIONBITMAP`; redundant
  even if the latter is removed. Drop the flag.
- [ ] **Synthetic mouse-warp event filtering**
  (`libs/window/src/win32_window.cpp:230-261`). The recentre warp
  generates a `MouseMove` with `dx == 0 && dy == 0` after every real
  move, doubling the event count. Filter by tracking `m_warp_pending`
  or by skipping zero-delta synthetic events.

**Acceptance:** Win32 build runs without "hollow" startup; cursor toggle
works any number of times; Alt+F4 closes the window via the system path;
mouse-look has no phantom zero-delta events.

### Maintenance Etape M3 — XCB window hardening

**Goal:** the same kind of clean-up for the X11/XCB backend on Linux.

**Scope:**

- [ ] **Invisible-cursor pixmap never initialised**
  (`libs/window/src/xcb_window.cpp:164-165`). 1×1 depth-1 pixmap
  contents are undefined per X11 spec; can render as a visible single
  pixel. Either zero the pixmap with a GC fill / `xcb_put_image`, or
  use the documented `XCB_CURSOR_NONE` fallback.
- [ ] **Cursor freed while grab still references it**
  (`libs/window/src/xcb_window.cpp:186`). `xcb_free_cursor` runs
  immediately after `xcb_grab_pointer` while the active grab still
  holds the resource — implementation-defined. Move the free into the
  `else` branch (after `xcb_ungrab_pointer`) and store the cursor
  handle in a member.
- [ ] **`screen_num` out-of-range crash**
  (`libs/window/src/xcb_window.cpp:51-56`). If `xcb_connect` returns
  `screen_num >= rem`, the iterator advances past the end and
  `iter.data` is null; the next access at `:71` segfaults. Guard with
  `iter.rem` and `logFatal` if no screen is found.
- [ ] **`WM_NAME` set as Latin-1, breaking UTF-8 titles**
  (`libs/window/src/xcb_window.cpp:82`). `XCB_ATOM_STRING` is Latin-1
  per ICCCM. Set `_NET_WM_NAME` with `UTF8_STRING` in addition (modern
  WMs prefer it).
- [ ] **Synthetic mouse-warp event filtering**
  (`libs/window/src/xcb_window.cpp:246-275`). Same root cause as the
  Win32 issue — recentre `xcb_warp_pointer` generates a synthetic
  MotionNotify. Filter by tracking a warp-pending flag.

**Acceptance:** Linux X11 build passes all window tests; cursor is
invisible during capture; UTF-8 titles render correctly on GNOME/KDE;
no crash on multi-screen setups with non-zero default screen.

### Acceptance Criteria

- [ ] Maintenance Etape M1 merged
- [ ] Maintenance Etape M2 merged
- [ ] Maintenance Etape M3 merged
- [ ] All findings from the 2026-04-26 `libs/` audit addressed

---

## Phase 9 — Engine Architecture + Integrated Subsystems

**Goal:** Extract the monolithic main.cpp into a proper engine structure
with tightly integrated physics, spatial audio, and environment sensory
subsystems. All subsystems share the same Vulkan device, GPU buffers,
BLAS/TLAS, and compute queues for maximum efficiency — no third-party
libraries (per VISION.md design principles).

### Planned Features

- **Engine class** — top-level coordinator that owns the Vulkan instance,
  device, swapchain, and render loop. Replaces the 2000+ line renderThread
  function with a structured pipeline.
- **Rendergraph** — DAG-based render pass scheduling with automatic
  barrier insertion, resource lifetime tracking, pass reordering, and
  transient resource aliasing. Replaces hand-coded barriers scattered
  across recordFrame(). Shader hot-reloading for development iteration.
- **Resource manager** — centralises GPU resource creation, staging
  uploads, and lifetime management via resource handles (indirection —
  the manager can move resources without invalidating references).
  Async loading with worker threads. Replaces ad-hoc buffer/image
  creation scattered across renderThread.
- **Async compute** — overlap post-processing compute with the next
  frame's mesh shader pass on separate compute queues.
- **Scene graph** — hierarchical entity/component system with transform
  parenting, spatial partitioning (BVH), and efficient iteration.
- **Rigid body physics** — collision detection (GJK/EPA), broadphase
  reusing the rendering BVH/BLAS, constraint solver, gravity.
  GPU-accelerated collision queries via the same compute queues.
- **Spatial audio** — HRTF-based 3D positional audio, ray-traced
  occlusion and reverb reusing the same BLAS/TLAS as rendering.
  Audio output to speakers (human mode) or bot interface (bot mode).
- **Environment sensory** — energy signature gradients (smell), surface
  contact feedback (touch), ambient field (temperature), damage (pain).
  Computed on the GPU using the same spatial data structures.
  All routed through the bot interface for AI perception.
- **Debug visualisation** — toggleable overlays: wireframe mode,
  frustum culling debug, BVH/BLAS visualisation, GPU profiler HUD
  (per-pass timestamps), heat map for overdraw. Essential for Phase
  8-10 development. In-house rendering (no ImGui dependency).
- **Software Vulkan for CI** — integrate Mesa lavapipe (CPU-based
  Vulkan 1.3 software rasteriser) into CI pipeline for GPU-less
  testing. Validates rendering logic, barrier correctness, and
  resource management without requiring physical GPU hardware.
- **Deterministic simulation** — same seed + same inputs = same outputs.
  Essential for AI training reproducibility (per VISION.md).
- **Variable simulation speed** — pause, 1x, 2x, fast-forward.
  `dt_seconds` reflects actual elapsed time. No zero-dt ticks during
  pause. Essential for AI training and observation (per VISION.md).

### Acceptance Criteria

- [ ] Engine class with clear module boundaries
- [ ] Rendergraph with automatic barrier management + resource aliasing
- [ ] Resource manager with handle indirection + async loading
- [ ] Shader hot-reloading for development iteration
- [ ] Async compute overlap
- [ ] Scene graph with spatial partitioning (shared BVH)
- [ ] Rigid body physics reusing rendering acceleration structures
- [ ] Spatial audio with ray-traced occlusion (shared BLAS/TLAS)
- [ ] Environment sensory system (smell, touch, temperature, pain)
- [ ] Debug visualisation overlays (wireframe, BVH, GPU profiler)
- [ ] Deterministic simulation (same seed = same output)
- [ ] Variable simulation speed (pause, 1x, 2x, fast-forward)
- [ ] Mesa lavapipe CI integration for GPU-less testing
- [ ] **Phase 9 complete — engine architecture + integrated subsystems**

---

## Phase 10 — Asset Pipeline + Procedural World

**Goal:** Import avatar and NPC body meshes from Blender via glTF 2.0
(own parser — no third-party libraries). Generate all Grid architecture
(buildings, data towers, platforms, barriers) procedurally. The Grid
builds itself; only creature bodies are authored externally.

### Planned Features

- **In-house glTF 2.0 parser** — load `.gltf`/`.glb` files with our
  own parser (no tinygltf, no fastgltf — write everything ourselves per
  design principle #2). Extract meshes, materials, skeleton hierarchy,
  skinning weights, and animations. New `libs/gltf/` static library.
- **Meshlet conversion** — convert imported triangle meshes to the
  engine's meshlet format. Extend the existing meshlet builder to accept
  arbitrary indexed mesh input (not just procedural terrain).
- **PBR material mapping** — map glTF metallic-roughness materials to
  the engine's Material SSBO. Texture support: base colour, normal,
  metallic-roughness, emissive, occlusion maps.
- **Skeletal animation** — bone hierarchy, joint transforms, GPU-
  accelerated skinning via compute shader. Animation blending and state
  machine for NPC and avatar bodies.
- **Procedural world generation** — algorithmic generation of Grid
  architecture: data towers, energy barriers, platforms, geometric
  structures. All built from code, not from scene files. Parameterised
  by seed for deterministic generation. GPU mesh shaders for on-the-fly
  geometry (AMD GPU Work Graphs technique — HPG 2024 — if Vulkan
  extension matures, otherwise CPU-side generation).
- **Data streams** — animated geometric tubes with flowing emissive
  particles. Pulsing light flowing along paths between Grid structures.
  Core Tron visual element (per VISION.md).
- **GPU particle system** — general-purpose compute particle SSBO for
  ambient effects: floating energy motes, data sparkles, Grid hum
  particles. The world should feel alive with tiny floating particles
  of light. Mesh shader rendered (point sprites or billboards).
- **Energy sources** — procedurally placed golden glow orbs floating
  and slowly rotating above the Grid. Pulsing warm emissive with bloom
  halo (Super Mario-inspired collectible aesthetic). Colour encodes
  energy value: bright gold = high energy, reddish = low energy. The
  AI brain has no text label — it must learn to estimate food value
  from colour alone (human players see a HUD label in Phase 12).
  Depletes when consumed (shrinks + dims), respawns over time at a
  different location. Warm tones stand out against the cool cyan-blue
  Grid palette — instantly recognisable as "food."
- **Texture streaming** — load textures on demand via VMA staging.
  Mip-chain generation on the GPU. Memory budget awareness.

### Acceptance Criteria

- [ ] In-house glTF 2.0 parser (own `libs/gltf/` library)
- [ ] Load glTF meshes + materials + skeleton + animation
- [ ] Convert glTF meshes to meshlet format
- [ ] PBR material mapping with texture support
- [ ] Skeletal animation with GPU skinning
- [ ] Procedural Grid architecture generation (data towers, barriers)
- [ ] Texture streaming with mip generation
- [ ] Data streams (animated emissive tubes with flowing particles)
- [ ] GPU particle system (ambient energy motes, sparkles)
- [ ] Energy sources (procedural, depletable, respawning)
- [ ] Blender → TronGrid round-trip verified for creature bodies
- [ ] **Phase 10 complete — asset pipeline + procedural world**

---

## Phase 11 — AI Avatar Integration

**Goal:** Load AI brains as DLL/SO plugins. The engine simulates the
creature's body; the brain DLL is the nervous system. The shared memory
interface carries raw sensory nerve signals in and muscle commands out —
no game state, no HUD data, no entity IDs. If a biological creature
couldn't perceive it through its nerves, the brain doesn't receive it.

See `docs/VISION.md` § AI Embodiment for the architecture.
Interface specification will be documented in `docs/AI_INTERFACE.md`.

### NPC Programmes

- **Programme entities** — simple NPCs: geometric wireframe shapes
  (cubes, pyramids, polyhedra) made of glowing lines. Patrol bots,
  guardian systems, data couriers — following coded routines.
- **Basic AI behaviours** — patrol paths, guard zones, flee from
  threats, pursue intruders. State machine driven, no brain DLL.
- **Derez on destruction** — programmes dissolve into geometric
  particles when destroyed (GPU compute particle system from Phase 10).

### Creature Body + Rendering

- **Avatar entity** — new entity type with skeletal body, joint
  constraints, mass distribution, identity colour. Rendered as organic
  curves with soft glow (visually distinct from the geometric world,
  per VISION.md § AI Visual Identity).
- **Controllable glow** — the brain controls glow intensity and colour
  hue as its primary emotional display (warm gold = content, cool blue
  = scared, red flicker = in pain).
- **Light trails** — moving entities leave persistent glowing streaks
  (ring buffer SSBO, ribbon geometry, emissive HDR + bloom, age fade).
- **Derez particle system** — entities dissolve into geometric particles
  (reuses GPU particle system from Phase 10).

### Sensory Interface (Engine → Brain)

Raw nerve signals per tick — the brain learns to interpret them:

- **Vision** — offscreen-rendered RGB + depth framebuffer from the
  creature's viewpoint. No labels, no bounding boxes. Resolution
  requested by the brain at init (starts small, grows as visual
  processing matures).
- **Hearing** — spatial audio arrivals with bearing, distance, loudness,
  and frequency bands (low/mid/high). NOT sound categories — the brain
  learns "rhythmic low-frequency = footsteps" from experience.
- **Smell (olfaction)** — multi-dimensional scent fingerprint vectors
  per source. Same entity always produces the same fingerprint. The
  brain must LEARN which fingerprints belong to which entities — no IDs.
  Intensity + rate of change + lateral gradient for steering.
- **Touch (mechanosensation)** — pressure per body zone. Ground contact
  flags. Multiple zones around the body, each reporting independently.
- **Proprioception** — joint angles, angular velocities, body curvature,
  speed, heading, ground contact. The creature's internal body sense.
- **Temperature** — core temperature + rate of change. Energy sources
  radiate warmth; void areas are cold.
- **Pain (nociception)** — localised pain events with zone, intensity,
  and type (sharp/blunt/burn/sting). Overall damage level.
- **Vibration** — ground vibration intensity, frequency, bearing.
  Footsteps, machinery, the Grid's hum.
- **Feeding signal** — energy received this tick + food contact flag.
  The brain tracks its own energy level internally.

### Motor Interface (Brain → Engine)

Muscle commands per tick — the engine applies physics:

- **Locomotion** — joint angle targets + muscle effort per joint. The
  engine applies torques constrained by mass, joint limits, friction.
  The brain discovers walking by experimenting with joint sequences.
  Simplified at early stages: forward + angular velocity only.
- **Head/eye control** — yaw, pitch, focus distance (independent of
  body movement).
- **Vocalisation** — pitch, volume, tonality parameters. The engine
  synthesises animal-like sounds that propagate spatially in the world.
  Hybrid approach: brain selects emotional intent, engine picks from
  organic sound library and modulates.
- **Mouth** — open amount + bite force for active feeding.
- **Glow** — intensity and colour hue for emotional expression.

### Brain Plugin Interface

- **C-linkage DLL/SO API**: `tg_brain_init`, `tg_brain_spawn`,
  `tg_brain_tick`, `tg_brain_shutdown`.
- **Shared memory nerve bundle** — sensory buffer (engine writes, brain
  reads) + motor buffer (brain writes, engine reads).
- **Staged rollout** — Stage 0 (blind worm: smell + touch + pain +
  temperature + simple locomotion), Stage 1 (insect: + vibration +
  directional light), Stage 2 (hamster: + vision + hearing + limbs +
  vocalisation), Stage 3 (full creature: + glow control + expression).
- **Phoenix model** — death is traumatic and remembered. Pre-death
  warning tick, then shutdown. Brain persists memories to disk.
  Respawn via init + spawn.
- **Persistence directory** — writable path for brain data (memories,
  learned associations).

### Acceptance Criteria

- [ ] Avatar entity with skeletal body and physics
- [ ] Offscreen rendering for bot vision (RGB + depth)
- [ ] Spatial audio routed to bot hearing interface
- [ ] Scent fingerprint system (stable per-entity vectors)
- [ ] Touch, proprioception, temperature, pain, vibration sensors
- [ ] Feeding mechanism (proximity-based → action-based)
- [ ] Locomotion via joint targets + physics (and simplified mode)
- [ ] Head/eye control independent of body
- [ ] Vocalisation synthesis (parametric or hybrid)
- [ ] Controllable glow (intensity + colour)
- [ ] C-linkage DLL/SO brain plugin loads and ticks
- [ ] Shared memory nerve bundle operational
- [ ] Light trails for moving entities
- [ ] Derez particle system
- [ ] NPC Programmes with basic AI behaviours (patrol, guard, flee)
- [ ] Stage 0 (blind worm) fully functional end-to-end
- [ ] **Phase 11 complete — AI avatar integration**

---

## Phase 12 — Cyberpunk HUD + Human Player Mode

**Goal:** Human player mode UI — a sleek cyberpunk heads-up display
rendered as a GPU-driven 2D overlay. The HUD gives human players
information that the AI brain must learn from raw senses. Not shown
in bot mode — the brain has no HUD, just like a biological creature
has no HUD.

**Design principle:** The HUD is a "cheat" that compensates for the
human's inability to smell, sense temperature, or process raw nerve
signals. The AI perceives all of this natively through the nerve bundle.

### Planned Features

- **MSDF text rendering** — in-house multi-channel signed distance field
  font atlas generator (sharper corners than plain SDF) and GPU text
  renderer. Crisp text at any resolution, single texture lookup per
  fragment.
- **Energy bar** — the player's energy level, styled as a neon-glow
  horizontal bar with scan line artefacts.
- **Health bar** — the player's health/damage level, separate from energy.
  Dims and flickers as damage increases.
- **Food value indicator** — floating text above energy orbs showing how
  much energy they are worth. The AI brain does not see this — it must
  estimate food value from colour (golden = high, reddish = low).
- **Entity labels** — floating text above NPCs, avatars, and other
  entities with name/type. The AI brain does not see these — it must
  learn to recognise entities from scent fingerprints and vision.
- **Energy signature visualisation** — visible coloured auras around
  scent sources that the AI can only "smell." Rendered as faint glowing
  halos with colour matching the scent fingerprint. Gives the human
  player a visual representation of the olfactory landscape.
- **Compass / heading indicator** — current facing direction, sector
  name. Minimal — just enough to orient.
- **Threat indicators** — directional damage flash on screen edges
  when hit. No enemy radar — the player uses their eyes and ears.
- **Scan line overlay** — faint CRT-style overlay that intensifies
  during low energy or damage. Sells the "inside a digital display"
  aesthetic.

### Acceptance Criteria

- [ ] MSDF font atlas generation + GPU text rendering
- [ ] Energy bar with neon-glow styling
- [ ] Health bar (separate from energy)
- [ ] Food value floating text above energy orbs
- [ ] Entity labels floating above NPCs and avatars
- [ ] Energy signature aura visualisation (scent → visual for humans)
- [ ] Compass / heading indicator
- [ ] Directional damage flash
- [ ] Scan line overlay (intensity varies with player state)
- [ ] HUD hidden in bot mode
- [ ] **Phase 12 complete — cyberpunk HUD**

---

## Phase 13 — Steam Publishing + Production Polish

**Goal:** Make TronGrid a shippable product on Steam. All the non-gameplay
features that separate a tech demo from a published game.

### Steamworks Integration

- **Steamworks SDK** — integrate the Steamworks API for achievements,
  leaderboards, cloud saves, Steam overlay, and screenshot capture.
  DLL/SO loaded at runtime — does not violate the "no third-party
  libraries" rule (Steam is the distribution platform, not a dependency).
- **Achievements** — milestone-based: first kill, first food, survive
  N seconds, explore N sectors, etc. Displayed in the Steam overlay.
- **Cloud saves** — automatic sync of human player progress and AI brain
  persistence data via Steam Cloud.
- **Leaderboards** — survival time, energy collected, sectors explored.
- **Steam Deck compatibility** — verified controller support, default
  controller config, appropriate resolution scaling.

### Production Features

- **Installer / launcher** — Steam handles distribution, but the game
  must handle first-run setup (Vulkan SDK check, GPU capability check,
  create default config file).
- **Configuration UI** — in-game settings menu: resolution, fullscreen/
  windowed, graphics quality presets (low/medium/high/ultra), key
  bindings, audio volume, MSAA level, bloom intensity. Persistent
  settings file (JSON or custom).
- **Input remapping** — rebindable keys and mouse sensitivity. Controller
  support (Xbox, PlayStation) via Steam Input API.
- **Screenshots** — Steam screenshot key (F12) integration. High-res
  screenshot mode (render at 2x resolution, save to disk).
- **Loading screen** — animated Tron-style loading indicator during
  world generation and asset loading.
- **Main menu** — minimal cyberpunk main menu: New Game, Continue,
  Settings, Quit. Bot mode bypass (--bot flag skips menu entirely).
- **Pause menu** — ESC opens pause overlay with Resume, Settings, Quit.
  Time stops during pause (already in Phase 9 variable speed).

### Quality Assurance

- **Crash reporting** — minidump generation on crash (Windows) + signal
  handler (Linux). Stack trace + GPU state logged.
- **Performance targets** — verified 4K@60+ on RTX 4090, 1080p@60+ on
  RTX 3060, 720p@30+ on GTX 1060. Automatic quality scaling.
- **Multi-GPU testing** — verified on NVIDIA (RTX 3060, 4090), AMD
  (RX 6700 XT, 7900 XTX), Intel Arc. Graceful fallback for missing
  features (no RT = no shadows, reduced MSAA).
- **Store assets** — 12+ marketing images at various resolutions,
  gameplay trailer video, store page description, tags, system
  requirements.

### Acceptance Criteria

- [ ] Steamworks SDK integrated (achievements, cloud saves, overlay)
- [ ] Steam Deck verified controller support
- [ ] Configuration UI with graphics quality presets
- [ ] Input remapping (keyboard + controller)
- [ ] Main menu + pause menu
- [ ] Loading screen
- [ ] Screenshots (F12 + high-res mode)
- [ ] Crash reporting (minidump + log)
- [ ] Performance verified on target hardware tiers
- [ ] Store page assets (images + trailer)
- [ ] **Phase 13 complete — Steam publishing ready**

---

## Backlog

- **Grid sectors / zones** — distinct areas with different visual
  characteristics, density, architecture style. Industrial sector,
  residential, the Outlands, data processing centres. Procedurally
  generated per seed. Different ambient sounds and fog density per zone.
- **Ambient world life** — the Grid hums and breathes without players.
  Recogniser patrol ships overhead (geometric, emissive), data stream
  pulses between towers, flickering Grid segments, background particle
  effects. Makes the world feel alive and inhabited.
- **Identity discs** — the signature Tron weapon. Thrown projectile
  with glowing trail, returns to thrower. Physics-based trajectory.
  Used by NPCs and player avatars in combat.
- **Combat system** — disc combat, energy attacks, derez mechanics.
  Damage model with pain feedback to AI brain via nerve bundle.
- **Accessibility** — colourblind mode (palette remapping for the
  cyan/orange/gold colour scheme), input remapping, subtitle system
  for vocalisations, screen reader support for menus.
- **Configuration system** — graphics quality presets, key bindings,
  audio volume, render resolution scaling. Persistent settings file.
- **Save/load** — human player progress persistence. World state
  snapshot + player inventory + position. AI brain handles its own
  persistence via the DLL interface.
- **Memory budget** — VMA budget tracking, streaming eviction policy,
  residency management for large scenes.
- **Multiplayer** — extract world state to authoritative server, network
  replication, prediction + reconciliation, multiple concurrent players.
  See `docs/VISION.md` § Future: Multiplayer. Deferred until single-player
  is fully polished.

---

## Journal

<!-- Reverse chronological — newest entries at the top. -->
<!-- Format: ### YYYY-MM-DD — Short title -->

### 2026-04-26 — Phase 8 Etape 40: transparency + ray-traced refraction

Etape 40 introduces a complete transparent-rendering path. Two new test entities — a
cyan-tinted glass tower at (-15, 8, -15) and a red energy-barrier pillar at (15, 4, 15),
each a 12-triangle box generated by a new `generateBox()` helper in `terrain.cpp` — sit
behind opaque entities in a deterministic SSBO ordering enforced by a startup partition
assertion. A new `Pipeline::transparentPipeline` shares the descriptor set layout, pipeline
layout, and task/mesh stages with the existing opaque pipeline; only the fragment entry
point (`fragTransparent` vs `fragMain`), depth-write state, and blend equations differ.
Premultiplied-alpha "over" blending (Porter-Duff: `srcA = ONE`, `dstA = ONE_MINUS_SRC_ALPHA`)
composes the transparent surfaces over the opaque pass + skybox. The task shader gains a
`base_object_index` push constant so a single dispatch covers any contiguous SSBO slice
(opaque uses `[0, N_opaque)`, transparent uses `[N_opaque, N_opaque + N_transparent)`). The
skybox draws between opaque and transparent passes within the same render pass; the
transparent dispatch must rebind the mesh-pipeline descriptor set after the skybox switches
to its own pipeline layout (a Vulkan layout-compatibility rule that bit me first try). The
shader does Snell-law refraction via HLSL `refract(I, N, eta)` — equivalent to the canonical
`T = η·I − (η·c1 + √k)·N` form, returns zero on negative discriminant for TIR fallback to
`reflect(I, N)`. Schlick Fresnel from `((n-1)/(n+1))²`, Beer-Lambert-lite tint via
`base_colour` on the refracted contribution, composite is `F·reflect + (1−F)·refract +
emissive`. To prevent self-intersection (refraction ray hitting its own back face and
sampling the glass material again), the glass + pillar BLAS geometries are flagged
non-opaque so a `RayQuery` Proceed() loop can skip candidates whose `CandidateInstanceID()`
matches the originating instance. Other BLASes stay `eOpaque` for traversal speed. The
pipeline outputs alpha 0.7 (premultiplied) so the explicit ray-traced result captures most
of the background while a 30 % dst bleed-through softens the visual toward "tinted glass
over a real background". Pillar emissive tuned to 1.5× (down from 4×) so the dielectric
refraction and reflection terms remain visible alongside the warm glow; emissive colour
matches the orb (material 1) byte-for-byte so the two warm light sources visually rhyme.
ReSTIR reservoir state stays exclusively owned by the opaque pass — `fragTransparent`
performs no reservoir reads or writes. Sub-etape verification gates: 40a (boxes appear as
opaque solids → confirms BLAS + scene wiring), 40b (transparents render translucent via
alpha blend → confirms pipeline + dispatch partition), 40c (terrain visible refracted
through glass → confirms Snell + Fresnel + self-instance skip). Full WBOIT (accumulation
and revealage targets, composite compute pass) was scoped out — premultiplied-alpha blending
is sufficient for two non-overlapping transparents and the WBOIT plumbing should land only
when the scene has enough overlapping translucent surfaces to justify it.

### 2026-04-24 — Phase 8 Etape 39: ray-traced ambient occlusion (RTAO)

Etape 39 adds ray-traced ambient occlusion. One cosine-weighted hemisphere ray
(Malley's method) per fragment per frame with TMax = 2 m captures local contact
shadows — terrace corners, neon-tube bases, under overhangs. Visibility stored
in the reservoir's repurposed `ao` slot (formerly `indirect_pad`, so the struct
stays 80 bytes) and accumulated via the canonical two-stage denoiser: temporal
EMA with alpha = 1/temporal_M, plus spatial averaging across geometrically-
similar neighbours inside the existing spatial-reuse loop (no new pass, no new
rays). Direct lighting is split into `direct_diffuse` and `direct_specular` so
AO multiplies the Lambertian lobes only (indirect GI + direct diffuse);
specular, RT reflection, and emissive stay at full brightness per canonical AO
practice. TLAS is unchanged — all existing 4 BLASes serve as occluders.
98 PRs merged.

### 2026-04-24 — Phase 8 rendering audit pass + literature-guided correction

Comprehensive 3-way parallel audit of the rendering code (Vulkan sync,
shader math, resource layout) surfaced ~20 findings. Corrections applied
in PR #97. The audit's initial "fixes" for four findings turned out to be
canonically wrong on visual verification and were reverted after
cross-checking Bitterli et al. (2020), LearnOpenGL IBL, Karis UE4 notes,
and RTXDI source: (1) M-clamp belongs on READ with proportional w_sum
scaling, not at write time — the W × M × p̂ = w_sum invariant is preserved
exactly if you scale both; (2) RT reflection is ADDED to analytic
Cook-Torrance spec, not a replacement — the two terms cover different
integrands and "replace" blacks out every pixel whose reflection ray
misses the TLAS; (3) skybox `ndc` reconstruction was already correct —
`-fvk-invert-y` flips SV_Position which moves the vertex carrying UV=(0,0)
to Vulkan-bottom, so the top pixel gets UV Y≈1 → ndc.y=+1 (math convention)
without manual flipping; (4) RR estimator's zero-on-reject IS the unbiased
term (`E[V] = p × (S/p) + (1-p) × 0 = S`), not a bias; skipping the EMA
update on reject would converge to `S/p` and saturate. Genuine bugs that
stuck: cross-frame reservoir `MemoryBarrier2` (the top suspect for the
user's "something feels off" ghosting symptoms), `SV_SampleIndex` guard
on MSAA reservoir writes, geometric similarity rejection via 80-byte
reservoir extension (shading_pos + oct-packed normal), front-facing
reflection guard + flat-N origin offset, GI ray flag unified to
`RAY_FLAG_NONE`, firefly clamp, `neonEmissiveColour` origin symmetry,
terrain OR-rule alignment with shader, swapchain lifecycle hardening
(image_avail sem rebuild, zero-extent guard before acquire, m_extent=0,
wider try/catch, reservoir clear + capacity assert on resize),
`prev_view_projection = identity()` frame-0 init. New memory added:
`feedback_audit_agent_verification.md` — verify audit-agent findings
against canonical literature for any domain-math claim before applying.
97 PRs merged.

### 2026-04-21 — Phase 8 Etape 38: single-bounce indirect GI + RT bug hunt

Etape 38 adds single-bounce indirect illumination and a thorough RT math audit.
Indirect GI uses cosine-weighted hemisphere sampling (Frisvad orthonormal basis)
with Russian roulette (probability scaled by surface luminance × 10), two-bounce
path tracing with hit normal from vertex SSBO and bounce shadow ray (no light
leaking). Temporal EMA accumulation via reservoir's `indirect` field (alpha
derived from pre-spatial-merge M, not post, to avoid dilution). Reservoir
struct extended to 64 bytes. Vertex struct extended to 48 bytes with
`smooth_normal` field. Shading-vs-reflection normal split: flat face normal
for Cook-Torrance shading preserves the Tron terraced aesthetic, smooth
normal computed from the raw (un-quantised) noise gradient drives reflections
for continuous mirror surfaces across terrace steps — standard normal-map
technique used for 20+ years. Per-material Fresnel F0 derived from `mat.ior`
via Snell's law. Obsidian roughness tuned to 0.15 to soften residual step-edge
discontinuities. Three bug-hunting sweeps found and fixed 13 RT issues:
temporal reprojection Y-flip (slangc `-fvk-invert-y` only negates
SV_Position), two-sided N leaking into reflections, back-facing reflection
handling, emissive pixels leaving stale reservoir data, reflection back-face
culling hiding tubes, GI bounce missing NdotL at hit, GI bounce light leaking
(no shadow ray), indirect EMA alpha diluted by spatial M, reservoir buffers
not zero-initialised (garbage first frame), reservoir bounds overflow above
4K, reflected non-emissive surfaces using flat AMBIENT instead of per-pixel
indirect GI, binding 4 needing eFragment stage flag, smooth normals initially
computed from quantised heightmap (later fixed to raw noise). Bloom strength
reduced from 0.19 to 0.095 (50% user request). Flat-grid reflection test
verified mirror math is correct. 94 PRs merged.

### 2026-04-06 — Phase 8 Etape 37c: ReSTIR DI spatial reuse (Etape 37 complete)

ReSTIR spatial reuse (Etape 37c) merges reservoirs from 5 random neighbours
within a 20-pixel radius. Reads from `reservoir_previous` (lagged one frame)
to avoid an extra compute pass — standard optimisation. Per-neighbour M clamped
to 100. Combined with temporal reuse (37b), the scene is dramatically cleaner
than raw 1-spp. Bloom strength reduced from 0.25 to 0.19 (user request).
Etape 37 (ReSTIR DI) is complete — acceptance criterion ticked. 89 PRs merged.

### 2026-04-06 — Phase 8 Etape 37b: ReSTIR DI temporal reuse

ReSTIR temporal reuse (Etape 37b) accumulates light samples across frames via
per-pixel reservoirs. Reservoir struct (48 bytes: y_pos, y_emissive, y_normal,
w_sum, M, W) stored in ping-pong SSBOs (bindings 10/11, 4K-capable allocation).
Fragment shader generates a candidate sample, reprojects to the previous frame
via `prev_clip_pos` (interpolated from mesh shader), reads the previous
reservoir, merges via RIS-weighted selection with M clamped to 20. Final shading
uses the reservoir's selected sample with shadow ray visibility. Screen
dimensions added to CameraUBO (replacing padding). `fragmentStoresAndAtomics`
enabled in device features for reservoir writes. Zero validation errors, zero
warnings, all tests pass. 88 PRs merged.

### 2026-04-06 — Phase 8 Etape 37a: emissive geometry sampling + motion vectors

Replaced the fake point light with physically correct direct lighting from
emissive geometry (Etape 37a). Neon tube quad geometry generated along every
terrain grid edge (14,336 cyan + 2,304 orange triangles) plus orb (528
triangles) — 17,168 emissive triangles total. Power-weighted CDF for triangle
selection, PCG hash RNG, uniform barycentric sampling, shadow ray with
Cook-Torrance BRDF evaluation. 1 sample per pixel — noisy but correct.
CameraUBO gains `prev_view_projection` (motion vectors) and `frame_count`
(random seed). Point light removed entirely. 4 BLASes in TLAS (terrain, orb,
cyan neon, orange neon). Emissive triangle SSBO at descriptor binding 9.
Reflection code now uses `CommittedInstanceID()` for per-material hit lookup.
Zero validation errors, zero warnings. 87 PRs merged.

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
