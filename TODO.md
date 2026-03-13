# TODO & Development Journal

A shared task list and journal for humans and AI assistants working on TronGrid.

---

## Current Etape: 1 — Internal Library Scaffolding

**Branch:** create from `main`, name `feat/libs-scaffolding`

**Goal:** Establish the `libs/` LEGO brick structure with the `test`, `signal`, and `window`
libraries. After this etape, `cmake --preset <name>` builds everything, `ctest` runs all tests,
and the existing window + main loop still works exactly as before.

**Read before starting:** `docs/ARCHITECTURE.md` § Internal Libraries, `STYLE.md`, `.clang-format`

### Steps (do in order)

#### 1. Create `libs/test_fixture/` — the test framework

Create the foundation brick that all other libraries' tests depend on.

```text
libs/
├── CMakeLists.txt              # add_subdirectory for each lib
└── test/
    ├── CMakeLists.txt           # add_library(test STATIC src/test.cpp)
    ├── include/test/test.hpp    # public header
    └── src/test.cpp             # implementation
```

The test library provides:

- `TEST_CHECK(expr)` — fails with file, line, and stringified expression
- `TEST_CHECK_EQUAL(a, b)` — fails showing both values
- `TEST_CHECK_THROWS(expr)` — fails if expr does not throw
- `test::register_test(name, fn)` — registers a test case
- `test::run_all()` — runs all registered tests, returns 0 on success, 1 on failure
- `TEST_CASE(name)` macro for auto-registration

Namespace: `test::`. No TronGrid prefixes. Header-only macros, compiled implementation.

Write a self-test: `libs/test_fixture/tests/CMakeLists.txt` + `libs/test_fixture/tests/test_self_tests.cpp`
that exercises all three macros (passing and failing cases). Wire into CTest.

#### 2. Create `libs/signal/` — move `src/signal.hpp`

Move the existing `src/signal.hpp` into a proper library brick.

```text
libs/signal/
├── CMakeLists.txt                  # header-only or STATIC lib
├── include/signal/signal.hpp       # moved from src/signal.hpp
└── tests/
    ├── CMakeLists.txt              # links: signal + test
    └── signal_tests.cpp            # emit/consume, thread safety, weak_ptr expiry
```

Namespace: `signal::` (rename `Signal<T>` → `signal::Signal<T>` or keep as-is — minimal change).
Update `src/main.cpp` include path if it references signal.hpp (currently it doesn't, so just
ensure the library compiles and tests pass).

#### 3. Create `libs/window/` — move `src/window/`

Move the existing window implementation into a library brick.

```text
libs/window/
├── CMakeLists.txt                  # add_library(window STATIC ...)
├── include/window/
│   ├── window.hpp                  # moved from src/window/window.hpp
│   ├── window_event.hpp            # moved from src/window/window_event.hpp
│   ├── win32_window.hpp            # moved from src/window/win32_window.hpp (Windows only)
│   └── xcb_window.hpp              # moved from src/window/xcb_window.hpp (Linux only)
├── src/
│   ├── win32_window.cpp            # moved from src/window/win32_window.cpp (Windows only)
│   └── xcb_window.cpp              # moved from src/window/xcb_window.cpp (Linux only)
└── tests/
    ├── CMakeLists.txt              # links: window + test
    └── window_tests.cpp            # basic construction/destruction, event queue
```

The window library handles platform detection internally via its own CMakeLists.txt.
Platform libs (XCB) are linked by the window library, not by the main app.

#### 4. Update root CMake and `src/CMakeLists.txt`

- Root `CMakeLists.txt`: add `add_subdirectory(libs)` before `add_subdirectory(src)`
- `src/CMakeLists.txt`: remove `window/*.cpp` sources, remove window include dirs, add
  `target_link_libraries(${PROJECT_NAME} PRIVATE window signal)` — CMake transitivity
  handles the rest
- Remove `volk.cpp` reference from `src/CMakeLists.txt` (it doesn't exist — Volk integration
  is a separate etape)
- Verify: `cmake --preset windows-msvc && cmake --build build/windows-msvc --config Debug`
  still produces a working executable
- Verify: `ctest --test-dir build/windows-msvc --config Debug` runs all library tests

#### 5. Commit and PR

- Branch: `feat/libs-scaffolding`
- One commit per logical step, or squash into one — either is fine
- PR against `main` using the project's PR template
- Mark completed tasks in this file

### Acceptance Criteria

- [ ] `libs/test_fixture/`, `libs/signal/`, `libs/window/` exist with proper structure
- [ ] All three libraries have CMakeLists.txt and compile as STATIC libraries
- [ ] `test_fixture` library self-tests pass
- [ ] `signal` library tests pass (emit, consume, empty, thread safety)
- [ ] `window` library tests pass (basic construction/destruction)
- [ ] `src/main.cpp` still compiles and runs (window + event loop works)
- [ ] `ctest` runs all test suites
- [ ] No `src/window/` directory remains (moved to `libs/window/`)
- [ ] No `src/signal.hpp` remains (moved to `libs/signal/`)
- [ ] Code follows `.clang-format` and `STYLE.md`
- [ ] British spelling in all comments and documentation

---

## Phase 0 — Remaining Etapes (after Etape 1)

### Etape 2 — Volk + Vulkan instance

- Integrate Volk for dynamic Vulkan loading (`VK_NO_PROTOTYPES`)
- Vulkan instance creation + debug messenger (validation layers in Debug)
- Physical device selection (prefer discrete GPU)

### Etape 3 — Device + swapchain

- Logical device + queue creation (graphics + present)
- VMA integration for memory allocation
- Swapchain setup (MAILBOX present mode)

### Etape 4 — Triangle on screen

- Graphics pipeline with `VkPipelineRenderingCreateInfo` (dynamic rendering, no VkRenderPass)
- Command buffer recording with `vkCmdBeginRendering` / `vkCmdEndRendering`
- Frame synchronisation (fences + semaphores, double/triple buffering)
- Hardcoded triangle (vertex + fragment shaders via Slang)
- Triangle on screen — Phase 0 complete

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
