# TODO & Development Journal

A shared task list and journal for humans and AI assistants working on TronGrid.

---

## Active Tasks

<!-- Add tasks here as they come up. Mark with [x] when done. -->

### Phase 0 — Foundation (see `docs/VISION.md` § Phased Roadmap)

- [ ] Integrate Volk for dynamic Vulkan loading
- [ ] Create platform window (Win32 / X11)
- [ ] Vulkan instance + debug messenger
- [ ] Physical device selection (prefer discrete GPU)
- [ ] Logical device + queue creation
- [ ] Swapchain setup (MAILBOX present mode)
- [ ] Render pass + framebuffers
- [ ] Graphics pipeline (vertex + fragment, hardcoded triangle)
- [ ] Command buffer recording + submission
- [ ] Frame synchronisation (fences + semaphores)
- [ ] Triangle on screen

---

## Backlog

<!-- Ideas, improvements, and tasks for later phases. -->

---

## Journal

<!-- Reverse chronological — newest entries at the top. -->
<!-- Format: ### YYYY-MM-DD — Short title -->

### 2026-03-08 — Project scaffolding complete

Adopted full GitHub infrastructure from git-proxy-mcp:

- Issue and PR templates
- Split CI workflows (ci_main + ci_pr) with markdownlint
- Release workflow (CMake-based, Windows + Linux artifacts)
- Cache cleanup workflow + script
- CONTRIBUTING, CODE_OF_CONDUCT, SECURITY, STYLE, CHANGELOG
- VS Code workspace config (settings, extensions, debug for all 5 presets)
- docs/ folder (ARCHITECTURE.md, VISION.md)
- Moved sources into src/ directory

Layered CMake setup: root CMakeLists.txt delegates to src/CMakeLists.txt (ready for future tests/ subdirectory).

The repo is now ready for Phase 0 implementation.
