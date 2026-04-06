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
| AI interface spec | `docs/AI_INTERFACE.md` (future) |
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
3. **Run `clang-format -i` on every changed C++ file before committing** — Allman braces for functions/namespaces, 4-space indent, 170 col
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
│   ├── ARCHITECTURE.md   # Technical architecture
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
│   ├── terrain.hpp/cpp   # Procedural terrain generator (value noise, flat shading)
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
Smith-GGX visibility, Schlick Fresnel) and dual-colour neon tube edges (cyan
primary + orange accent on major grid lines). HDR framebuffer
(`R16G16B16A16_SFLOAT`), compute post-process pass (ACES fitted RRT+ODT
tonemapping with AP1 hue preservation, exact sRGB encoding, swapchain
`B8G8R8A8_UNORM` with storage writes). Bloom with soft glow halos (Karis extraction, mip chain downsample, 3×3
tent-filter upsample, HDR composite with tunable strength). 8× MSAA with
full sample-rate shading, screen-space derivative wireframe antialiasing
(fwidth-based smoothstep), automatic GPU capability fallback. Procedural
cyberpunk skybox (value noise data fog clouds, horizon gradient, animated
drift). Per-material PBR via material SSBO (binding 8, data-driven). Cinematic
post-process (chromatic aberration, cool colour grade, vignette, scan lines).
RT hard
shadows and single-bounce reflections via inline ray query (`VK_KHR_ray_query`,
BLAS/TLAS). Mesh shaders (task + mesh + fragment), per-object frustum culling,
meshlet pipeline. Entity/component scene with SoA arrays. Code quality:
Clang-Tidy, sanitisers, GPU validation, -Werror. See `docs/VISION.md` §
Phased Roadmap for the full 14-phase plan (0–13).

## Off Limits

**`CODE_OF_CONDUCT.md`** — Do not modify.
**`LICENCE`** — Do not modify (legal document).
