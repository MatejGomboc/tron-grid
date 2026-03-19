# TODO & Development Journal

A shared task list and journal for humans and AI assistants working on TronGrid.

---

## Completed Etapes

- **Etape 1** — Internal Library Scaffolding. Completed 2026-03-13. PR #18.
- **Etape 2** — Vulkan Instance, Validation, and Device. Completed 2026-03-13. PR #20.
- **Etape 3** — Swapchain, Command Buffers, and Clear Screen. Completed 2026-03-15. PRs #22, #23, #24.
- **Etape 4** — Triangle on Screen (Phase 0 Finale). Completed 2026-03-19. PR #26.

---

## Backlog

<!-- Ideas, improvements, and tasks for later phases. -->

---

## Journal

<!-- Reverse chronological — newest entries at the top. -->
<!-- Format: ### YYYY-MM-DD — Short title -->

### 2026-03-19 — Phase 0 complete

Triangle on screen with VMA, Slang shaders, dynamic rendering, logging library,
and comprehensive STYLE.md. All 5 CI presets pass. Codebase fully modernised:
camelCase functions, `m_` members, `vk::` scoped enums, brace initialisation,
GPL v3 licence headers, Qt-style doxygen.

### 2026-03-13 — Architecture crystallised

Documented the full engine architecture across VISION, ARCHITECTURE, and AI_INTERFACE docs:

- Single-player engine, one AI brain (DLL/SO) per instance
- Console-first startup: human mode creates window, bot mode stays console
- All core subsystems (rendering, physics, audio, sensory) written in-house
- Internal static libraries under `libs/` (LEGO bricks, plain namespaces, submodule-ready)
- Modern Vulkan patterns: vk::raii, dynamic rendering, component model, Signal<T>, rendergraph
- AI bot interface via shared memory, staged sensory protocol, standalone C header
- Future goals: MMO multiplayer (Phase 10), VR mode

### 2026-03-08 — Project scaffolding complete

Adopted full GitHub infrastructure from git-proxy-mcp:

- Issue and PR templates
- Split CI workflows (ci_main + ci_pr) with markdownlint
- Release workflow (CMake-based, Windows + Linux artefacts)
- Cache cleanup workflow + script
- CONTRIBUTING, CODE_OF_CONDUCT, SECURITY, STYLE, CHANGELOG
- VS Code workspace config (settings, extensions, debug for all 5 presets)
- docs/ folder (ARCHITECTURE.md, VISION.md)
- Moved sources into src/ directory

Layered CMake setup: root CMakeLists.txt delegates to src/CMakeLists.txt (ready for future libs/ subdirectory).

The repo is now ready for Phase 0 implementation.
