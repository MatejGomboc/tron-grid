# Development Environment Setup

Complete guide to setting up the TronGrid development environment from scratch.

---

## Prerequisites

| Tool | Minimum Version | Download |
|------|----------------|----------|
| CMake | 3.16 | <https://cmake.org/download/> |
| Vulkan SDK | 1.4.335.0 | <https://vulkan.lunarg.com/sdk/home> |
| Git | any recent | <https://git-scm.com/downloads> |

### Compiler (pick one per platform)

**Windows:**

| Compiler | Preset | Notes |
|----------|--------|-------|
| MSVC (Visual Studio 2022+) | `windows-msvc` | Recommended. Install "Desktop development with C++" workload |
| Clang-CL (LLVM for Windows) | `windows-clang-cl` | Requires LLVM installed alongside MSVC |
| MinGW-w64 (GCC for Windows) | `windows-mingw` | Install via MSYS2 or standalone |

**Linux:**

| Compiler | Preset | Notes |
|----------|--------|-------|
| GCC 12+ | `linux-x11-gcc` | Usually pre-installed or `sudo apt install g++` |
| Clang 15+ | `linux-x11-clang` | `sudo apt install clang` |

---

## Windows Setup

### Step 1 — Install Visual Studio

Download and install [Visual Studio 2022 Community](https://visualstudio.microsoft.com/downloads/)
(or newer). During installation, select the **"Desktop development with C++"** workload.

Alternatively, install just the
[Build Tools for Visual Studio](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio)
if you prefer VS Code over the Visual Studio IDE.

### Step 2 — Install CMake

Download and install CMake from <https://cmake.org/download/>. During installation,
select **"Add CMake to the system PATH"**.

Verify:

```bash
cmake --version
```

### Step 3 — Install Vulkan SDK

Download the Vulkan SDK from <https://vulkan.lunarg.com/sdk/home>. Install with
**all components selected** (especially Volk, VMA, Slang/slangc, spirv-val, spirv-opt,
and validation layers).

The installer sets the `VULKAN_SDK` environment variable automatically. Verify:

```bash
echo %VULKAN_SDK%
# Should print something like: C:\VulkanSDK\1.4.335.0
```

### Step 4 — Clone and build

```bash
git clone https://github.com/MatejGomboc/tron-grid.git
cd tron-grid
cmake --preset windows-msvc
cmake --build build/windows-msvc --config Debug
```

### Step 5 — Run tests

```bash
ctest --preset windows-msvc-debug
```

### Step 6 — Run the application

```bash
build\windows-msvc\src\Debug\TronGrid.exe
```

Use WASD + mouse (right-click to toggle mouse look) to fly through the cube grid.
Press ESC to close.

---

## Linux Setup (Ubuntu/Debian)

### Step 1 — Install compiler and tools

```bash
sudo apt update
sudo apt install -y build-essential cmake git
```

For Clang instead of GCC:

```bash
sudo apt install -y clang
```

### Step 2 — Install XCB development headers

Required for window creation:

```bash
sudo apt install -y libxcb1-dev
```

### Step 3 — Install Vulkan SDK

Follow the official instructions at <https://vulkan.lunarg.com/sdk/home> for your
distribution. On Ubuntu:

```bash
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.4.335.0-noble.list https://packages.lunarg.com/vulkan/1.4.335.0/lunarg-vulkan-1.4.335.0-noble.list
sudo apt update
sudo apt install vulkan-sdk
```

Verify:

```bash
echo $VULKAN_SDK
vulkaninfo --summary
```

### Step 4 — Clone and build

```bash
git clone https://github.com/MatejGomboc/tron-grid.git
cd tron-grid
cmake --preset linux-x11-gcc
cmake --build build/linux-x11-gcc --config Debug
```

### Step 5 — Run tests

```bash
ctest --preset linux-x11-gcc-debug
```

### Step 6 — Run the application

```bash
./build/linux-x11-gcc/src/Debug/TronGrid
```

---

## VS Code Setup (Recommended IDE)

### Step 1 — Install VS Code

Download from <https://code.visualstudio.com/>.

### Step 2 — Install recommended extensions

Open the project folder in VS Code. A notification will appear offering to install
recommended extensions. Accept — or install manually:

- **C/C++ Extension Pack** (ms-vscode.cpptools-extension-pack)
- **CMake Tools** (ms-vscode.cmake-tools)
- **clangd** (llvm-vs-code-extensions.vscode-clangd) — inline Clang-Tidy warnings
- **Clang-Format** (xaver.clang-format)
- **Slang Language Extension** (shader-slang.slang-language-extension)
- **EditorConfig** (editorconfig.editorconfig)

### Step 3 — Select CMake preset

Press `Ctrl+Shift+P` → "CMake: Select Configure Preset" → choose your platform preset
(e.g., `windows-msvc` or `linux-x11-gcc`).

### Step 4 — Build

Press `F7` or use the CMake Tools build button in the status bar.

### Step 5 — Run/Debug

Press `F5` to launch with the debugger. Debug configurations for all presets are
pre-configured in `.vscode/launch.json`.

---

## CMake Presets Reference

All builds use CMake presets. Run with `cmake --workflow --preset=<name>` for a full
configure → build → test cycle.

### Configure presets

| Preset | Platform | Compiler | Notes |
|--------|----------|----------|-------|
| `windows-msvc` | Windows | MSVC | Primary Windows preset |
| `windows-clang-cl` | Windows | Clang-CL | Clang with MSVC ABI |
| `windows-mingw` | Windows | MinGW GCC | GCC via MSYS2 |
| `linux-x11-gcc` | Linux | GCC | Primary Linux preset |
| `linux-x11-clang` | Linux | Clang | Clang-Tidy integration |

### Sanitiser presets (Linux Clang only)

| Preset | What it checks |
|--------|---------------|
| `linux-x11-clang-asan` | AddressSanitizer + UndefinedBehaviorSanitizer |
| `linux-x11-clang-tsan` | ThreadSanitizer |

### Workflow shortcuts

```bash
# Full cycle: configure + build Debug + build Release + test Debug + test Release
cmake --workflow --preset=windows-msvc

# Sanitiser workflow
cmake --workflow --preset=linux-x11-clang-asan
cmake --workflow --preset=linux-x11-clang-tsan
```

---

## Troubleshooting

### `VULKAN_SDK` not found

CMake error: `Could NOT find Vulkan`. Ensure the Vulkan SDK is installed and the
`VULKAN_SDK` environment variable is set. Restart your terminal after installation.

### `slangc` not found

CMake error: `Could not find slangc`. The Slang compiler ships with the Vulkan SDK.
Ensure you installed the SDK with all components. Check that `$VULKAN_SDK/bin/slangc`
(or `$VULKAN_SDK/Bin/slangc.exe` on Windows) exists.

### `libxcb` not found (Linux)

Build error: `xcb/xcb.h: No such file`. Install the XCB development headers:

```bash
sudo apt install -y libxcb1-dev
```

### VMA header not found

Build error: `vk_mem_alloc.h: No such file`. The VMA header ships with the Vulkan SDK.
Ensure the SDK was installed with all components (including "Vulkan Memory Allocator").

### Warnings treated as errors

This project enforces zero warnings (`-Werror` / `/WX`). If you see a warning treated
as an error, fix the warning — do not disable `-Werror`.

---

*See [CONTRIBUTING.md](../CONTRIBUTING.md) for coding style, PR workflow, and commit conventions.*
*See [STYLE.md](../STYLE.md) for the complete code style guide.*
