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

Place the initialiser list on the same line as the constructor when it fits within 170 columns.
When it overflows, break after the colon with 4-space indentation:

```cpp
// Fits on one line
Win32Window::Win32Window(const WindowConfig& config, LoggingLib::Logger& logger) : Window(logger)
{
}

// Overflows — break after colon
Swapchain::Swapchain(const Device& device, VkSurfaceKHR surface, uint32_t width, uint32_t height, LoggingLib::Logger& logger) :
    m_logger(&logger), m_device(&device), m_surface(surface)
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

*Last updated: 2026-03-17*
