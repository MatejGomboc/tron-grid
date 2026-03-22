# Style Guide

Code style conventions for TronGrid.

---

## General Rules

| Rule | Setting |
|------|---------|
| Indentation | 4 spaces (no tabs) |
| Max line length | 170 characters |
| Charset | UTF-8 |
| Final newline | Always |
| Trailing whitespace | Trim (except Markdown) |

These rules are enforced by `.editorconfig`. Install the EditorConfig plugin for your editor:

- **VS Code:** [EditorConfig for VS Code](https://marketplace.visualstudio.com/items?itemName=EditorConfig.EditorConfig)

---

## Single Source of Truth

Avoid duplicating information across files. Each piece of information should have one canonical location.

| Information | Canonical Source |
|-------------|-----------------|
| Build commands | `README.md` § Building |
| Formatting rules | `.clang-format`, `.editorconfig` |
| Security policy | `SECURITY.md` |
| Roadmap & phases | `docs/VISION.md` § Phased Roadmap |

**Guidelines:**

- Reference the canonical source instead of duplicating content
- If information must appear in multiple places (e.g., PR template checklists), keep it minimal
- When updating information, update the canonical source first
- Cross-reference using `filename` § Section Name format

---

## C++

### Formatting

Use `.clang-format` (LLVM-based). CI does not currently enforce this, but all code should be formatted before committing.

Key settings:

| Setting | Value |
|---------|-------|
| Brace style | Allman for functions/namespaces, attached for classes/structs/enums |
| Indent | 4 spaces |
| Column limit | 170 |
| Pointer alignment | Left (`int* ptr`) |
| Braces on bodies | Always — even single-line `if`/`else`/`for`/`while` (`InsertBraces: true`) |

### Naming Conventions

| Item | Convention | Example |
|------|------------|---------|
| Namespaces | PascalCase | `TronGrid` |
| Types / Classes / Structs | PascalCase | `SwapchainImage` |
| Functions / Methods | camelCase | `createDevice`, `pollEvent` |
| Constants | SCREAMING_SNAKE_CASE | `MAX_FRAMES_IN_FLIGHT` |
| Variables | snake_case | `frame_index` |
| Member variables | m_snake_case | `m_device_handle` |
| Macros | SCREAMING_SNAKE_CASE | `VK_NO_PROTOTYPES` |

### Language Standard

C++20 (and **NOT** beyond it!). Do not throw exceptions in project code. Catch exceptions from third-party libraries
(e.g., vulkan-hpp `vk::raii`) at API boundaries only. For unrecoverable errors in project code,
log via `LoggingLib::Logger::logFatal()` and call `std::abort()` followed by a return statement to exit that function.

### Type Explicitness

Do not use `auto` — write the explicit type so the reader never has to guess. The only exception
is where the type is impossible to spell (lambdas).

```cpp
// Correct
vk::raii::Pipeline pipeline = device.createGraphicsPipeline(cache, info);
uint32_t count = static_cast<uint32_t>(items.size());

// Wrong
auto pipeline = device.createGraphicsPipeline(cache, info);
auto count = static_cast<uint32_t>(items.size());

// Exception — lambdas have unspellable types
auto on_resize = [&](const WindowLib::WindowEvent& ev) { ... };
```

### Attributes

Use `[[nodiscard]]` on all functions that return a value the caller must not silently discard —
getters, factory functions, query functions.

```cpp
[[nodiscard]] const vk::raii::Device& get() const;
[[nodiscard]] uint32_t graphicsFamilyIndex() const;
```

### Operator Precedence

Use explicit parentheses when combining arithmetic, bitwise, or increment/decrement operators
with comparison or logical operators. Do not rely on the reader knowing precedence rules:

```cpp
// Correct — each sub-expression is explicit
if ((++frame_counter) % 60 == 0) { ... }
if ((a & mask) != 0) { ... }
if ((file_size <= 0) || (file_size % sizeof(uint32_t) != 0)) { ... }

// Wrong — relies on implicit precedence
if (++frame_counter % 60 == 0) { ... }
if (a & mask != 0) { ... }
```

Simple boolean chains with comparisons are fine without extra parentheses — the precedence
of `==`, `!=`, `<`, `>` relative to `&&` and `||` is universally understood:

```cpp
// Fine — no extra parentheses needed
if (width == 0 || height == 0) { ... }
if (!indices.isComplete() || !hasRequiredExtensions(device)) { ... }
```

### Constants

Use `constexpr` for compile-time constants. Name them `SCREAMING_SNAKE_CASE`. Do not use
plain `const` or magic numbers where `constexpr` applies.

```cpp
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
constexpr float QUEUE_PRIORITY = 1.0f;
```

### Modern C++20 Idioms

Prefer `std::ranges::` algorithms over `std::` + `.begin()/.end()`:

```cpp
// Correct
std::ranges::find_if(devices, predicate);

// Wrong
std::find_if(devices.begin(), devices.end(), predicate);
```

Prefer `std::string_view` for read-only string parameters and comparisons — avoids
unnecessary heap allocations.

Use C++20 designated initialisers for aggregate/struct initialisation where applicable:

```cpp
vk::InstanceCreateInfo info{
    .flags = {},
    .pApplicationInfo = &app_info,
    .enabledLayerCount = static_cast<uint32_t>(layers.size()),
    .ppEnabledLayerNames = layers.data(),
};
```

### Include Order

All `#include` directives are flush — no blank lines between groups. Order:

1. Same-module headers (`"device.hpp"`)
2. Project library headers (`<log/logger.hpp>`, `<window/window.hpp>`)
3. Standard library headers (`<vector>`, `<string>`)

```cpp
#include "device.hpp"
#include "instance.hpp"
#include <log/logger.hpp>
#include <window/window.hpp>
#include <cstdint>
#include <string>
#include <vector>
```

### Comment Alignment

Do not column-align trailing comments. Use a single space before `//` or `//!<`:

```cpp
// Correct
SignalsLib::Signal<LogMessage> m_queue; //!< Thread-safe message queue.
std::thread m_worker; //!< Background writer thread.
bool m_stop{false}; //!< Set to true when the logger is shutting down.

// Wrong — padded to align
SignalsLib::Signal<LogMessage> m_queue;  //!< Thread-safe message queue.
std::thread m_worker;                   //!< Background writer thread.
bool m_stop{false};                     //!< Set to true when the logger is shutting down.
```

The same applies to enum values — no extra spaces between the value and its comment.

### Constructor Initialiser Lists

Always break after the colon. Each initialiser gets its own line with 4-space indentation.
A single initialiser is one line; multiple initialisers are one per line:

```cpp
Win32Window::Win32Window(const WindowConfig& config, LoggingLib::Logger& logger) :
    Window(logger)
{
}

Swapchain::Swapchain(const Device& device, VkSurfaceKHR surface, uint32_t width, uint32_t height, LoggingLib::Logger& logger) :
    m_logger(&logger),
    m_device(&device),
    m_surface(surface)
{
}
```

### Member Initialisation

Use brace initialisation `{}` for all member default values — not `= value` assignment:

```cpp
// Correct
uint32_t m_width{0};
bool m_tracked{false};
const Device* m_device{nullptr};
vk::raii::Device m_device{nullptr};

// Wrong
uint32_t m_width = 0;
bool m_tracked = false;
```

### Vulkan C++ Bindings

Use **vulkan-hpp** with the `vk::raii` namespace for all Vulkan objects. RAII wrappers own their
handles and destroy them automatically — no manual `vkDestroy*` or `device.destroy*` calls.

```cpp
// Correct — vk::raii owns the handle
vk::raii::Image image = device.createImage(image_info);

// Forbidden — non-RAII type used for ownership
vk::Image image = device.createImage(image_info);
```

Non-RAII types (`vk::Image`, `vk::Device`, etc.) are acceptable only as transient parameters
to API calls that don't transfer ownership.

See `docs/ARCHITECTURE.md` § Vulkan Resource Management for the full ownership hierarchy.

### Resource Ownership

RAII everywhere. Use `vk::raii` for Vulkan objects, `std::unique_ptr` for single-owner heap
objects, and `std::shared_ptr` / `std::weak_ptr` only for signal ownership (see
`docs/ARCHITECTURE.md` § Signal-Based Communication).

### Doxygen Comments

All doxygen comments must be proper sentences — capital letter start, period end.

| Style | Use | Example |
|-------|-----|---------|
| `//!` | Single-line brief | `//! Returns the current extent.` |
| `//!<` | Inline member | `uint32_t m_width; //!< Client-area width in pixels.` |
| `/*! */` | Multi-line block | See below |

Multi-line doxygen blocks use 4-space indented content:

```cpp
/*!
    Records a command buffer that transitions the swapchain image to
    colour attachment, clears it to dark teal, and transitions to
    present layout.
*/
```

Use Qt-style backslash commands (`\param`, `\return`, `\brief`) — not Javadoc `@` prefix:

```cpp
/*!
    Selects the best physical device for rendering.

    \param instance The Vulkan instance to enumerate devices from.
    \param surface The target surface used to check present support.
    \return The selected physical device, or std::nullopt if none is suitable.
*/
```

Licence headers use plain `/* */` (not doxygen) with the full GPL v3 notice:

```cpp
/*
    Copyright (C) 2026 Matej Gomboc https://github.com/MatejGomboc/tron-grid

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/
```

---

## Slang Shaders

Follow the C++ conventions where applicable — Slang is syntactically close to HLSL/C++.

**Use HLSL flavour exclusively** — not GLSL. Slang supports both, but we standardise on HLSL
for consistency:

| HLSL (correct) | GLSL (wrong) |
|----------------|--------------|
| `float2`, `float3`, `float4` | `vec2`, `vec3`, `vec4` |
| `float3x3`, `float4x4` | `mat3`, `mat4` |
| `mul(A, B)` | `A * B` for matrices |
| `ConstantBuffer<T>` | `uniform` block |
| `StructuredBuffer<T>` | `buffer` block |
| `SV_Position`, `SV_Target` | `gl_Position`, layout location |
| `(float3x3)matrix` | constructor cast |

### Formatting

| Setting | Value |
|---------|-------|
| Indent | 4 spaces |
| Brace style | Allman for functions, attached for structs |
| Column limit | 170 |

### Naming Conventions

| Item | Convention | Example |
|------|------------|---------|
| Structs | PascalCase | `VSInput`, `VSOutput` |
| Entry points | camelCase | `vertMain`, `fragMain` |
| Struct members | snake_case | `position`, `colour` |
| Constants | SCREAMING_SNAKE_CASE | `MAX_LIGHT_COUNT` |

### Semantics

Use HLSL-style semantics (`POSITION`, `COLOR0`, `SV_Position`, `SV_Target`) — not
`[[vk::location(N)]]` unless explicit location control is required.

### Entry Points

Mark entry points with `[shader("vertex")]`, `[shader("fragment")]`, etc. Use descriptive
names — not `main`:

```slang
[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    ...
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target
{
    ...
}
```

### Licence Header

Same GPL v3 `/* */` block as C++ files.

---

## YAML (GitHub Actions)

### Indentation

**4 spaces** for structure levels — aligned with project-wide convention.

```yaml
jobs:
    build:
        name: Build
        runs-on: ubuntu-latest

        steps:
            - name: Checkout
              uses: actions/checkout@v6

            - name: Build
              run: cmake --workflow --preset=linux-x11-gcc
```

### List Item Indentation

List items use **2-space continuation** from the `-` character (standard YAML behaviour):

```yaml
updates:
    - package-ecosystem: "github-actions"
      directory: "/"
      schedule:
        interval: "weekly"
```

### Multi-line Scripts (`run: |`)

Shell script content inside `run: |` blocks uses **4-space indentation** for shell constructs (if/else, loops):

```yaml
            - name: Example step
              shell: bash
              run: |
                if [[ -n "$VAR" ]]; then
                    echo "Variable is set"
                else
                    echo "Variable is not set"
                fi
```

### Structure

- Blank line between top-level keys (`on`, `env`, `jobs`)
- Blank line between jobs
- Blank line before `steps:` in complex jobs
- Comments on their own line, not inline

---

## JSON

### Indentation

**4 spaces**.

```json
{
    "key": "value",
    "nested": {
        "item": 123
    }
}
```

---

## Markdown

### Headings

Use ATX-style headings with blank lines before and after:

```markdown
## Section Title

Content here.
```

### Lists

Use `-` for unordered lists, `1.` for ordered lists.

### Code Blocks

Always specify the language:

````markdown
```cpp
int main()
{
    return 0;
}
```
````

### Trailing Whitespace

Markdown files are exempt from trailing whitespace trimming (needed for line breaks).

### Linting

Use `markdownlint-cli2` to lint Markdown files. Configuration is in `.markdownlint.json` and `.markdownlint-cli2.jsonc`.

```bash
npx markdownlint-cli2 "**/*.md"
```

---

## British Spelling

Use British spelling in all documentation, comments, and user-facing strings:

| American | British |
|----------|---------|
| color | colour |
| optimize | optimise |
| behavior | behaviour |
| center | centre |
| license | licence |
| meter | metre |
| synchronize | synchronise |
| initialize | initialise |

Code identifiers may use American spelling where it matches library/API conventions (e.g., Vulkan API names).

---

## Code Quality Tooling

### Compiler Warnings

Warnings are errors on all compilers — zero-warning policy:

- **MSVC:** `/W4 /WX`
- **GCC/Clang:** `-Wall -Wextra -Wpedantic -Werror`

### Static Analysis (Clang-Tidy)

Configuration in `.clang-tidy`. Runs via `clangd` in VS Code. Bugprone, analyser, and
concurrency checks are promoted to errors. To suppress a specific check on a line:

```cpp
int x = legacy_function(); // NOLINT(bugprone-unused-return-value)
```

### Runtime Sanitisers

CMake presets for sanitiser builds (Linux Clang only):

```bash
# AddressSanitizer + UndefinedBehaviorSanitizer
cmake --workflow --preset=linux-x11-clang-asan

# ThreadSanitizer (cannot combine with ASan)
cmake --workflow --preset=linux-x11-clang-tsan
```

### Shader Validation

- `spirv-val` validates compiled SPIR-V at build time (malformed bytecode = build failure)
- `slangc -warnings-as-errors all` treats shader warnings as errors

### Vulkan Validation (Debug Builds)

Enabled automatically in debug builds via `VkValidationFeaturesEXT`:

- **GPU-Assisted Validation** — instruments shaders at runtime
- **Synchronisation Validation** — deep barrier analysis
- **Best Practices** — non-optimal API usage warnings

### MSVC Debug Leak Detection

`_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF)` is enabled at startup in
MSVC debug builds. Reports C++ heap leaks on exit.

---

*Last updated: 2026-03-22*
