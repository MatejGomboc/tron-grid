# TODO & Development Journal

A shared task list and journal for humans and AI assistants working on TronGrid.

---

## Active Tasks

<!-- Add tasks here as they come up. Mark with [x] when done. -->

### Phase 0 — Foundation (see `docs/VISION.md` § Phased Roadmap)

**Goal:** Triangle on screen — prove the toolchain end-to-end.

#### Internal libraries (`libs/`)

- [ ] Create `libs/` directory structure and root `libs/CMakeLists.txt`
- [ ] Implement `test` library (TEST_CHECK, TEST_CHECK_EQUAL, TEST_CHECK_THROWS)
- [ ] Move `src/signal.hpp` → `libs/signal/` as a proper static library
- [ ] Move `src/window/` → `libs/window/` as a proper static library
- [ ] Wire up CTest so `ctest` runs all library tests

#### Vulkan infrastructure

- [ ] Integrate Volk for dynamic Vulkan loading (`VK_NO_PROTOTYPES`)
- [ ] Vulkan instance creation + debug messenger
- [ ] Physical device selection (prefer discrete GPU)
- [ ] Logical device + queue creation (graphics + present)
- [ ] VMA integration for memory allocation
- [ ] Swapchain setup (MAILBOX present mode)

#### Rendering (dynamic rendering — no VkRenderPass)

- [ ] Graphics pipeline with `VkPipelineRenderingCreateInfo` (no render pass)
- [ ] Command buffer recording with `vkCmdBeginRendering` / `vkCmdEndRendering`
- [ ] Frame synchronisation (fences + semaphores, double/triple buffering)
- [ ] Hardcoded triangle (vertex + fragment shaders via Slang)
- [ ] Triangle on screen

---

## Backlog

<!-- Ideas, improvements, and tasks for later phases. -->

---

## Journal

<!-- Reverse chronological — newest entries at the top. -->
<!-- Format: ### YYYY-MM-DD — Short title -->

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
