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
| Vision | `docs/VISION.md` |
| Architecture | `docs/ARCHITECTURE.md` |
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
3. **Use `.clang-format`** — Allman braces for functions/namespaces, 4-space indent, 170 col
4. **Use British spelling** in documentation, comments, and user-facing strings
5. **Use GPL v3-or-later** licence headers
6. **Use `vk::raii`** for all Vulkan object ownership
7. **Use `Signal<T>`** for inter-system communication (see `src/signal.hpp`)
8. **Use dynamic rendering** instead of traditional render passes

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
│   └── VISION.md         # Project vision and roadmap
├── src/                  # C++ source files
│   ├── CMakeLists.txt    # Target definition, platform detection
│   └── main.cpp          # Entry point (currently hello world)
├── .clang-format         # LLVM-based, Allman braces, 170 col
├── .editorconfig         # 4-space indent, UTF-8, 170 col
├── .gitattributes        # Line ending normalisation
├── .gitignore            # Build artifacts, IDE files
├── .markdownlint.json    # Markdown linting rules
├── .markdownlint-cli2.jsonc  # Markdownlint ignore config
├── CMakeLists.txt        # Root build configuration (delegates to src/)
├── CMakePresets.json     # 5 presets (MSVC, Clang-CL, MinGW, GCC, Clang)
├── CHANGELOG.md          # Version history
├── CODE_OF_CONDUCT.md    # Contributor Covenant 3.0
├── CONTRIBUTING.md       # Contributor guidelines
├── LICENCE               # GPL v3
├── README.md             # Public-facing project overview
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
| Inter-system comms | `Signal<T>` queues | Thread-safe, lifetime-safe via weak_ptr |
| Render orchestration | Rendergraph (DAG) | Automatic pass ordering and synchronisation |
| Coordinate system | Right-handed, Y-up | Matches glTF, most tools |
| Units | Metres | Physically-based lighting |
| Colour space | Linear internal, sRGB output | Correct blending |
| HDR range | 16-bit float | Emissive glow needs headroom |
| Meshlet size | 64 verts, 124 triangles | NVIDIA optimal |
| Descriptor model | Fully bindless | No rebinding, GPU-driven |
| Present mode | MAILBOX | Low latency, no tearing |
| Core subsystems | All in-house | 3D rendering, physics, audio, sensory — no third-party libs |
| External deps | Minimal | Vulkan SDK, Volk, vulkan-hpp, VMA, Slang — nothing else |

## Current Status

Currently in **Phase 0 — Foundation** (triangle on screen).
See `docs/VISION.md` § Phased Roadmap for the full 10-phase plan.
See `TODO.md` for the active task checklist and development journal.

## Off Limits

**`CODE_OF_CONDUCT.md`** — Do not modify.
**`LICENCE`** — Do not modify (legal document).
