# TronGrid — AI Assistant Context

## The Vision (Read First!)

See `docs/VISION.md` for the full architecture:

TronGrid is a single-player game engine and renderer for a digital world where an AI creature
perceives and navigates through rendered images. One AI brain (DLL/SO plugin) can be loaded per
instance. Multiplayer (MMO) is a future goal — see `docs/VISION.md` § Future: Multiplayer.

## What Is This Project?

A Vulkan-based rendering engine (C++20) targeting 4K @ 60+ FPS with full ray tracing on NVIDIA RTX 4090.

## Quick Reference

| Resource | Location |
|----------|----------|
| Dev environment setup | `docs/DEV_ENV_SETUP.md` |
| Vision | `docs/VISION.md` |
| Architecture | `docs/ARCHITECTURE.md` |
| PBR reference | `docs/PBR.md` |
| AI interface spec | `docs/AI_INTERFACE.md` |
| Style guide | `STYLE.md` |
| Contributing | `CONTRIBUTING.md` |

## Architecture Reference (Vulkan Tutorial)

When implementing engine systems, consult the official Vulkan tutorial series:

| Topic | URL |
|-------|-----|
| Architectural patterns | <https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/02_architectural_patterns.html> |
| Component systems | <https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/03_component_systems.html> |
| Resource management | <https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/04_resource_management.html> |
| Rendering pipeline | <https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/05_render_pipeline.html> |
| Event systems | <https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/06_event_systems.html> |

## Critical Rules

### NEVER Do These

1. **NEVER use macOS, Wayland, or mobile** — Windows (Win32) and Linux (X11) only
2. **NEVER link Vulkan statically** — use Volk (dynamic loading)
3. **NEVER use GLSL/HLSL directly** — use Slang for shaders
4. **NEVER use American spelling** — British English everywhere (colour, optimise, metres)
5. **NEVER push to main** — use feature branches and PRs
6. **NEVER over-engineer** — no abstractions until there's a concrete second use case
7. **NEVER use non-RAII vulkan-hpp types for ownership** — use `vk::raii` namespace only
8. **NEVER use VkRenderPass / VkFramebuffer** — use dynamic rendering (`VK_KHR_dynamic_rendering`)
9. **NEVER call manual `vkDestroy*` / `device.destroy*`** — RAII handles all cleanup
10. **NEVER add third-party physics, audio, or sensory libraries** — all core subsystems are written in-house (no PhysX, no FMOD, no OpenAL, etc.)

### ALWAYS Do These

1. **Define `VK_NO_PROTOTYPES`** globally
2. **Use CMake presets** (`cmake --preset <name>`)
3. **Run `clang-format -i` on every changed C++ file before committing** — Allman braces, 4-space indent, 170 col. Target `.cpp`/`.hpp` only.
   NEVER pass `.slang` files (clang-format mangles HLSL attributes/semantics into invalid syntax — format Slang by hand per `STYLE.md` § Slang Shaders).
4. **Use British spelling** in documentation, comments, and user-facing strings
5. **Use GPL v3-or-later** licence headers
6. **Use `vk::raii`** for all Vulkan object ownership
7. **Use `SignalsLib::Signal<T>`** for inter-system communication (see `libs/signals/include/signal/signal.hpp`)
8. **Use dynamic rendering** instead of traditional render passes

## CI/CD Notes

- **Dependabot** only updates `actions/cache` references in `.github/workflows/*.yml`. The custom
  composite actions (`.github/actions/setup-vulkan-windows/action.yml` and
  `.github/actions/setup-vulkan-linux/action.yml`) also use `actions/cache` but are **not** covered
  by Dependabot. When reviewing a Dependabot PR that bumps `actions/cache`, manually update the
  hash and version comment in both custom actions to match.

## Project Structure

```
tron_grid/
├── .claude/              # AI assistant context (you are here)
│   ├── commands/         # Custom slash commands (/check-style, /check-spelling, /audit-code)
│   └── CLAUDE.md         # This file — repo map
├── .github/              # CI workflows, templates, Vulkan SDK actions
│   ├── actions/          # Custom Vulkan SDK setup actions
│   ├── ISSUE_TEMPLATE/   # Bug report, feature request templates
│   ├── scripts/          # Cache cleanup utility script
│   ├── workflows/        # ci_main, ci_pr, release, cleanup_caches
│   ├── CODEOWNERS        # Repository ownership
│   ├── dependabot.yml    # Daily GitHub Actions updates
│   └── PULL_REQUEST_TEMPLATE.md
├── .vscode/              # Editor settings, extensions, debug configs
├── docs/                 # Extended documentation
│   ├── AI_INTERFACE.md   # AI brain plugin interface (staged protocol, shared memory)
│   ├── ARCHITECTURE.md   # Technical architecture
│   ├── DEV_ENV_SETUP.md  # Development environment setup guide
│   ├── PBR.md            # PBR reference (microfacets, Cook-Torrance, obsidian, HDR)
│   └── VISION.md         # Project vision and roadmap
├── libs/                 # Internal static libraries (LEGO bricks)
│   ├── testing/          # Testing library (TEST_CHECK, TEST_CHECK_EQUAL, TEST_CHECK_THROWS)
│   ├── signals/          # Thread-safe SignalsLib::Signal<T> message queues
│   ├── logging/          # Background LoggingLib::Logger via Signal<LogMessage>
│   ├── math/             # Header-only MathLib (Vec2/3/4, Mat4, Quat, projection)
│   ├── window/           # Platform windowing — WindowLib (Win32 / XCB)
│   └── .../              # physics, audio, etc. (future)
├── src/                  # C++ source files (main application)
│   ├── main.cpp          # Entry point, event loop, render thread
│   ├── instance.hpp/cpp  # Vulkan instance + debug messenger
│   ├── device.hpp/cpp    # GPU selection + logical device
│   ├── surface.hpp/cpp   # Platform Vulkan surface
│   ├── swapchain.hpp/cpp # Swapchain with MAILBOX + old-swapchain reuse
│   ├── pipeline.hpp/cpp  # Mesh shader pipeline, descriptors, push constants
│   ├── allocator.hpp/cpp # VMA RAII wrapper (Allocator + AllocatedBuffer + AllocatedImage)
│   ├── meshlet.hpp/cpp   # Meshlet data structure + generation (64v/84t)
│   ├── components.hpp    # Entity components (Transform, MeshID, Bounds)
│   ├── scene.hpp         # Scene — flat SoA entity/component arrays
│   ├── camera.hpp        # Free-flight camera (quaternion, WASD + mouse look)
│   ├── task.slang        # Slang task shader (per-object frustum culling)
│   ├── mesh.slang        # Slang mesh + fragment shader (meshlet rendering)
│   ├── postprocess.slang # Slang compute post-process (ACES tonemapping, sRGB encoding)
│   ├── bloom_downsample.slang # Slang compute bloom (extraction + mip chain downsample)
│   ├── skybox.slang       # Slang procedural cyberpunk skybox (value noise clouds)
│   ├── terrain.hpp/cpp   # Procedural terrain + neon tube geometry (value noise, flat shading)
│   ├── triangle.slang    # Legacy vertex + fragment shader (kept for reference)
│   ├── cull.slang        # Legacy compute cull shader (kept for reference)
│   ├── volk.cpp          # Volk dynamic loader translation unit
│   ├── vma.cpp           # VMA implementation translation unit
│   └── CMakeLists.txt    # Target definition, shader compilation
├── .clang-format         # LLVM-based, Allman braces, 170 col
├── .editorconfig         # 4-space indent, UTF-8, 170 col
├── .gitattributes        # Line ending normalisation
├── .gitignore            # Build artifacts, IDE files
├── .markdownlint.json    # Markdown linting rules
├── .markdownlint-cli2.jsonc  # Markdownlint ignore config
├── CMakeLists.txt        # Root build configuration (delegates to src/)
├── CMakePresets.json     # 5 presets (MSVC, Clang-CL, MinGW, GCC, Clang) + 2 sanitiser presets
├── CHANGELOG.md          # Version history
├── CODE_OF_CONDUCT.md    # Contributor Covenant 3.0
├── CONTRIBUTING.md       # Contributor guidelines
├── LICENCE               # GPL v3
├── README.md             # Public-facing project overview
├── tsan_suppressions.txt # ThreadSanitizer suppression (glibc false positive)
├── SECURITY.md           # Vulnerability reporting
├── STYLE.md              # Code style conventions
└── TODO.md               # Task list and development journal
```

## Building

```bash
# Windows (from VS Developer Command Prompt or with MSVC in PATH)
cmake --preset windows-msvc
cmake --build build/windows-msvc --config Debug

# Linux
sudo apt-get install libxcb1-dev
cmake --preset linux-x11-gcc
cmake --build build/linux-x11-gcc --config Debug
```

## Key Design Decisions

| Decision | Choice | Why |
|----------|--------|-----|
| Vulkan C++ bindings | vulkan-hpp `vk::raii` | RAII ownership, type safety, no manual destroy |
| Rendering model | Dynamic rendering | No VkRenderPass/VkFramebuffer boilerplate |
| Entity model | Component-based | Composition over inheritance |
| Inter-system comms | `SignalsLib::Signal<T>` queues | Thread-safe, lifetime-safe via weak_ptr |
| Render orchestration | Rendergraph (DAG) | Automatic pass ordering and synchronisation |
| Coordinate system | Right-handed, Y-up | Matches glTF, most tools |
| Units | Metres | Physically-based lighting |
| Colour space | Linear internal, sRGB output | Correct blending |
| HDR range | 16-bit float | Emissive glow needs headroom |
| Meshlet size | 64 verts, 84 triangles | Reduced from 124 for barycentric vertex duplication |
| Descriptor model | Fully bindless | No rebinding, GPU-driven |
| Present mode | MAILBOX | Low latency, no tearing |
| Core subsystems | All in-house | 3D rendering, physics, audio, sensory — no third-party libs |
| External deps | Minimal | Vulkan SDK, Volk, vulkan-hpp, VMA, Slang — nothing else |
| Internal libraries | `libs/` static libs | Self-contained LEGO bricks, plain namespaces, future submodule-ready |
| Logging | `LoggingLib::Logger` | Background thread, `Signal<LogMessage>`, severity routing |
| Threading model | Event-driven, separate render thread | Main thread sleeps on events, render thread sleeps on `Signal<RenderEvent>` |
| Testing | Own `TestingLib` | `TEST_CHECK`, `TEST_CHECK_EQUAL`, `TEST_CHECK_THROWS` — no third-party |

## Current Status

Procedural Tron terrain with PBR obsidian floor (Cook-Torrance BRDF: GGX NDF,
Smith-GGX visibility, Schlick Fresnel, metallic-aware F0) and dual-colour
neon tube edges (cyan primary + orange accent on major grid lines). HDR
framebuffer (`R16G16B16A16_SFLOAT`), compute post-process pass (ACES fitted
RRT+ODT tonemapping with AP1 hue preservation, exact sRGB encoding, swapchain
`B8G8R8A8_UNORM` with storage writes). Bloom with soft glow halos (Karis
extraction, mip chain downsample, 3×3 tent-filter upsample, HDR composite
with tunable strength). 8× MSAA with full sample-rate shading and
`SV_SampleIndex`-guarded reservoir writes (only sample 0 stores — samples
1..N still compute for coverage but skip the SSBO write), screen-space
derivative wireframe antialiasing (fwidth-based smoothstep), automatic GPU
capability fallback. Procedural cyberpunk skybox (value noise data fog clouds,
horizon gradient, animated drift). Per-material PBR via material SSBO
(binding 8, data-driven). Cinematic post-process (chromatic aberration with
signed-int clamp, cool colour grade, vignette, 3-pixel-period scan lines).
Emissive area light sampling — real neon tube quad geometry (binding 9
emissive triangle SSBO) replaces the point light; power-weighted CDF, PCG
hash RNG, Cook-Torrance BRDF evaluation, shadow ray visibility. ReSTIR DI
temporal + spatial reuse (bindings 10/11 ping-pong reservoir SSBOs,
**80-byte per-pixel** — adds `shading_pos` and octahedral-packed
`shading_normal` for geometric rejection) — canonical Bitterli et al.
M-clamp (20× current for temporal, 100 for spatial) with **proportional
`w_sum` scaling** so the `W × M × p̂ = w_sum` invariant holds across frames.
Temporal reprojection via `prev_clip_pos` in MeshOutput, spatial merges 5
random neighbours within 20-pixel radius; both passes reject reservoirs
whose stored shading surface differs from the current pixel (position delta
≤ 1 m, normal dot ≥ 0.906 per RTXDI defaults). **Cross-frame `MemoryBarrier2`**
at the top of `recordFrame` makes the previous submission's fragment-stage
reservoir writes visible to this frame's reservoir reads — submission-order
alone is insufficient under the Vulkan memory model. Single-bounce indirect
GI via cosine-weighted hemisphere sampling (Malley's method) with Russian
roulette (unbiased estimator: EMA updates every frame with zero on reject,
not skipped), hit normal from vertex SSBO + bounce shadow ray, temporal EMA
accumulation in the reservoir. Firefly clamp (per-sample max 30) and tighter
GI-bounce distance floor (0.04 m²) bound the variance without breaking
averaging. **RTAO** (Etape 39) — one cosine-weighted hemisphere ray per
fragment, TMax 2 m for local contact shadows, temporal EMA into the
reservoir's `ao` slot + spatial averaging across geometrically-similar
neighbours; applied to diffuse terms only (indirect GI and the split
`direct_diffuse` lobe), preserving specular and RT reflection at full
brightness per canonical AO practice. **Transparency + ray-traced
refraction** (Etape 40) — two new transparent test entities (cyan-tinted
glass tower, red energy-barrier pillar, materials 4–5 with opacity < 1)
rendered through a dedicated transparent pipeline that shares the
descriptor set layout + pipeline layout with the opaque pipeline (only
fragment entry point, depth-write, and blend state differ). Premultiplied
alpha blending (Porter-Duff "over": `srcA = ONE`, `dstA =
ONE_MINUS_SRC_ALPHA`). Task shader gains a `base_object_index` push
constant so a single dispatch covers any contiguous SSBO range; entities
are sorted opaque-first transparent-last with a fail-fast partition assert
at startup. `fragTransparent` does Snell-law refraction via HLSL
`refract()` (η = 1/IOR entering, IOR exiting), TIR fallback to reflection
on negative discriminant, Schlick Fresnel from `((n-1)/(n+1))²`, and a
Beer-Lambert-lite `base_colour` tint on the refracted contribution.
Composite is `F·reflect + (1−F)·(refract·base_colour) + emissive` with
output alpha 0.7 (premultiplied) — the explicit ray query captures most
of the background while a 30 % dst bleed-through softens the look toward
glass-y. Glass + pillar BLAS geometries are flagged non-opaque so the
inline `RayQuery` Proceed() loop can skip self-instance hits via
`CandidateInstanceID()`; other BLASes stay `eOpaque` for traversal speed.
ReSTIR reservoir state stays exclusively owned by the opaque pass —
`fragTransparent` performs no reservoir reads or writes. Skybox is drawn
between the opaque and transparent passes in the same render pass; the
transparent dispatch rebinds the mesh-pipeline descriptor set after the
skybox switches the bound pipeline layout. 6 BLASes total (terrain, orb,
cyan neon, orange neon, glass tower, energy-barrier pillar). `fragmentStoresAndAtomics` enabled for fragment shader
reservoir writes. RT single-bounce reflections with per-material hit lookup
via `CommittedInstanceID()`, front-facing guard, flat `N` for origin offset,
and `prev_view_projection` reprojection when sampling the hit pixel's stored
indirect radiance. Shading-vs-reflection normal split: flat face normal for
Cook-Torrance shading preserves the Tron terraced aesthetic, smooth normal
computed from raw (un-quantised) noise gradient drives reflections for
continuous mirror surfaces across terrace boundaries (standard normal-map
technique). Two-sided flip applied to both `N` and `N_smooth` together so
reflections stay consistent on camera-facing back sides. Hybrid composite
is additive per canonical PBR: `(indirect + direct_diffuse) × ao +
direct_specular + emissive + reflected_colour × F_view`. Per-material
Fresnel F0 derived from `mat.ior` via Snell's law, unified across direct
and reflection paths, `lerp(F0_dielectric, base_colour, metallic)`.
Swapchain lifecycle hardened: `image_available_semaphores` rebuilt on
resize (symmetry with present semaphores), zero-extent guard BEFORE acquire
(`waitIdle` doesn't reset semaphore state), `acquireNextImage` catches
`SurfaceLostKHR` + generic `SystemError`, reservoir ping-pong zero-cleared
and capacity-checked on resize (`static_assert(MAX_FRAMES_IN_FLIGHT == 2)`
at the binding site). Mesh shaders (task + mesh + fragment), per-object
frustum culling, meshlet pipeline. Entity/component scene with SoA arrays.
Code quality: Clang-Tidy, sanitisers, GPU validation, -Werror. See
`docs/VISION.md` § Phased Roadmap for the full 14-phase plan (0–13).

## Off Limits

**`CODE_OF_CONDUCT.md`** — Do not modify.
**`LICENCE`** — Do not modify (legal document).
