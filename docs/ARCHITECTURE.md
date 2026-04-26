# Architecture

Technical architecture of TronGrid.

> This document describes the engine architecture. Sections covering future phases
> are marked accordingly. See `TODO.md` for the development journal.

---

## Overview

TronGrid is a Vulkan-based engine written in C++20, targeting modern discrete GPUs with full ray
tracing support. All core subsystems — 3D rendering, 3D physics, 3D spatial audio, and 3D
environment sensory (energy signatures) — are written in-house with no third-party libraries. This
enables deep optimisation: shared spatial data structures, fused GPU passes, and a single scene
traversal serving all subsystems.

External dependencies are minimal: Vulkan SDK, Volk (dynamic loader), vulkan-hpp (`vk::raii`),
VMA (AMD Vulkan Memory Allocator), and Slang (shader compiler). Nothing else.

The engine follows a **modular, component-based architecture** with RAII ownership, signal-based
inter-system communication, and a rendergraph for pipeline orchestration.

```text
┌────────────────────────────────────────────────────────────────┐
│                        Application                             │
│  Window (Win32 / X11) · Input · Main loop                      │
├────────────────────────────────────────────────────────────────┤
│                     Tightly-Coupled Engine Core                 │
│  3D Rendering · 3D Physics · 3D Spatial Audio                  │
│  (share Vulkan device, buffers, compute queues)                │
│                                                                │
│  Components · Signals · Resource handles                       │
├────────────────────────────────────────────────────────────────┤
│                       Rendergraph                              │
│  Pass DAG · Automatic synchronisation · Barriers               │
├────────────────────────────────────────────────────────────────┤
│                    Vulkan Backend (RAII)                        │
│  vk::raii wrappers · Swapchain · Command buffers               │
├────────────────────────────────────────────────────────────────┤
│                       GPU Resources                            │
│  Buffers · Images · Descriptors (bindless)                     │
├────────────────────────────────────────────────────────────────┤
│                      Volk (loader)                             │
│  Dynamic Vulkan function pointer resolution                    │
└────────────────────────────────────────────────────────────────┘
        ▲
        │ shared memory interface only (bot mode)
        ▼
┌────────────────────────────────────────────────────────────────┐
│  AI Brain (DLL/SO) — external, loosely coupled, independent    │
└────────────────────────────────────────────────────────────────┘
```

### Internal Libraries (`libs/`)

The project is built from self-contained static libraries — LEGO bricks that snap together via
CMake `target_link_libraries`. Each library has its own include directory, source, and test suite.

```text
libs/
├── testing/                   # testing library (foundation brick)
│   ├── CMakeLists.txt         # add_library(testing STATIC ...)
│   ├── include/testing/testing.hpp
│   ├── src/testing.cpp
│   └── tests/
├── signals/                   # thread-safe SignalsLib::Signal<T> queues
│   ├── include/signal/signal.hpp
│   └── tests/
├── logging/                   # background LoggingLib::Logger
│   ├── include/log/logger.hpp
│   ├── src/logger.cpp
│   └── tests/
├── math/                      # header-only MathLib (Vec, Mat4, Quat, projection)
│   ├── include/math/vector.hpp, matrix.hpp, quaternion.hpp, projection.hpp
│   └── tests/
├── window/                    # platform windowing — WindowLib (Win32 / XCB)
│   ├── include/window/window.hpp
│   ├── src/win32_window.cpp, xcb_window.cpp
│   └── tests/
└── ...
```

**Rules:**

- **PascalCase + "Lib" suffix namespaces** — libraries use `SignalsLib`, `LoggingLib`,
  `WindowLib`, `TestingLib`. They are general-purpose and could be extracted into separate
  repositories as git submodules later
- **Each library is self-contained** — own `CMakeLists.txt`, own `include/<lib>/` directory,
  own `tests/` directory with unit tests linking the `testing` library
- **Plain CMake target names** — `testing`, `signals`, `logging`, `math`, `window`
- **Static libraries only** (except `math` which is header-only INTERFACE) — linked into the final TronGrid executable
- **Testing library is itself a library** — `testing` is the foundation brick; all other
  libraries' tests link against it. Macros: `TEST_CHECK`, `TEST_CHECK_EQUAL`, `TEST_CHECK_THROWS`

### Coupling Model

The rendering, physics, and spatial audio engines are **tightly coupled** — they run in the
same process, share the same Vulkan device, and can share GPU buffers, compute queues, and
synchronisation primitives. This is deliberate: tight integration enables optimisations like
GPU-driven physics, compute-based audio propagation, and unified memory management.

The AI brain is the opposite — it is **loosely coupled** via a shared memory interface. The
brain never touches Vulkan objects, engine internals, or world state directly. It reads sensory
data from a buffer and writes actions to another. This boundary is strict and intentional.

---

## Key Design Decisions

| Decision | Choice | Why |
|----------|--------|-----|
| Vulkan loading | Volk (dynamic) | No static linking; `VK_NO_PROTOTYPES` defined globally |
| Vulkan C++ bindings | vulkan-hpp (`vk::raii`) | RAII wrappers, type safety, no manual `destroy` calls |
| Rendering model | Dynamic rendering (`VK_KHR_dynamic_rendering`) | No VkRenderPass/VkFramebuffer boilerplate; inline attachments |
| Shader language | Slang | Modern, composable; compiles to SPIR-V |
| Resource ownership | RAII everywhere | `vk::raii` objects, `std::unique_ptr`, no manual cleanup |
| Inter-system communication | `Signal<T>` (thread-safe queues) | Decoupled, lock-free-ish, lifetime-safe via `weak_ptr` |
| Entity model | Component-based | Composition over inheritance, modular, extensible |
| Render orchestration | Rendergraph (DAG) | Automatic pass ordering, synchronisation, resource aliasing |
| Coordinate system | Right-handed, Y-up | Matches glTF and most authoring tools |
| Units | Metres | Physically-based lighting needs real-world scale |
| Colour space | Linear internal, sRGB output | Correct blending and light accumulation |
| HDR range | 16-bit float | Emissive glow needs headroom beyond [0, 1] |
| Meshlet size | 64 vertices, 84 triangles | Reduced from 124 for barycentric vertex duplication |
| Shadow, reflection, refraction technique | Inline ray query (`VK_KHR_ray_query`) | Simple, no SBT; shadows + single-bounce reflections + Snell-law refraction in fragment shaders |
| Volumetric scattering | Frustum-aligned voxel grid (160×90×64 froxels) + compute shaders | AAA-standard (Frostbite, UE, Unity HDRP); decouples cost from screen fillrate; fits naturally with the existing ReSTIR / RT compute infrastructure |
| Descriptor model | Fully bindless | No rebinding between draws; GPU-driven compatible |
| Present mode | MAILBOX | Low latency, no tearing |
| Engine subsystems | Tightly coupled (render, physics, audio) | Share Vulkan device, buffers, compute queues for efficiency |
| AI brain interface | Loosely coupled (shared memory only) | Brain is an independent DLL/SO; no access to engine internals |

---

## Vulkan Resource Management: `vk::raii`

All Vulkan objects use the `vk::raii` namespace from vulkan-hpp. These wrappers own their
underlying Vulkan handle and destroy it automatically when the C++ object goes out of scope —
no manual `vkDestroy*` or `device.destroy*` calls anywhere in the codebase.

```cpp
// Good — RAII, automatic cleanup
vk::raii::Device device = /* ... */;
vk::raii::Image image = device.createImage(image_info);
vk::raii::DeviceMemory memory = device.allocateMemory(alloc_info);
// image and memory are destroyed when they leave scope

// Bad — manual lifecycle, error-prone, forbidden
vk::Device device = /* ... */;
vk::Image image = device.createImage(image_info);
// ... must remember device.destroyImage(image) — never do this
```

**Rule: never use non-RAII vulkan-hpp types (`vk::Device`, `vk::Image`, etc.) for ownership.**
Non-RAII types are acceptable only as transient handles passed to Vulkan API calls that don't
transfer ownership.

### Ownership Hierarchy

Vulkan objects form a natural ownership tree. The `vk::raii` wrappers enforce this — each object
holds a reference to its parent and destroys itself in the correct order:

```text
vk::raii::Instance
  └── vk::raii::Device
        ├── vk::raii::Swapchain
        ├── vk::raii::CommandPool
        │     └── vk::raii::CommandBuffer (allocated from pool)
        ├── vk::raii::Image
        ├── vk::raii::DeviceMemory
        ├── vk::raii::ImageView
        ├── vk::raii::Sampler
        ├── vk::raii::ShaderModule
        ├── vk::raii::Pipeline
        ├── vk::raii::Semaphore
        └── vk::raii::Fence
```

Store objects as members or in `std::unique_ptr` / `std::vector`. Destruction order is handled
by C++ member destruction order (reverse of declaration). Plan struct layouts accordingly.

---

## Component-Based Entity Model *(target design)*

Entities in TronGrid are **containers of components**, not nodes in an inheritance hierarchy.
Each component has a single responsibility. Components are added, removed, and queried by type.

```cpp
// Composition, not inheritance
Entity player("ai_player_01");
player.add_component<TransformComponent>(position, rotation);
player.add_component<MeshComponent>(mesh_handle, material_handle);
player.add_component<CameraComponent>(fov, aspect, near, far);
player.add_component<AiBrainComponent>(brain_dll_path);
```

### Why Components Over Inheritance

Deep class hierarchies (`GameObject → PhysicalObject → Character → Player → AiPlayer`) create
rigidity, code duplication, and bloated base classes. Components avoid all of this:

- **Flexible** — any combination of behaviours, no diamond inheritance
- **Modular** — add rendering to a physics object without touching either system
- **Data-oriented** — components of the same type can be stored contiguously for cache efficiency
- **Testable** — test a component in isolation without constructing an entire entity tree

### Component Lookup

Use compile-time type IDs for O(1) component access instead of `dynamic_cast`:

```cpp
auto* transform = entity.get_component<TransformComponent>(); // O(1) hash lookup
```

---

## Signal-Based Communication

Systems communicate through `SignalsLib::Signal<T>` — a thread-safe, typed message queue. This
avoids tight coupling between systems that don't need to know about each other.

```cpp
// libs/signals/include/signal/signal.hpp
namespace SignalsLib
{
    template <typename T> class Signal {
    public:
        //! Thread-safe enqueue.
        void emit(const T& data);

        //! Thread-safe dequeue; returns true if a value was consumed.
        [[nodiscard]] bool consume(T& out);

        //! Returns true if the queue is empty.
        [[nodiscard]] bool empty() const;

        //! Returns the number of pending messages.
        [[nodiscard]] std::size_t size() const;

    private:
        std::queue<T> m_pending; //!< Queued messages.
        mutable std::mutex m_mutex; //!< Protects the queue.
    };
}
```

### Ownership Model

- **Receiver owns** the signal: `std::shared_ptr<SignalsLib::Signal<T>>`
- **Sender holds** a weak reference: `std::weak_ptr<SignalsLib::Signal<T>>`

When the receiver is destroyed, the `shared_ptr` dies, the `weak_ptr` expires, and the sender
knows to stop — no dangling pointers, no manual unregistration.

```cpp
// Window system (receiver) creates and owns the signal.
std::shared_ptr<SignalsLib::Signal<ResizeEvent>> resize_signal{std::make_shared<SignalsLib::Signal<ResizeEvent>>()};

// Renderer (sender) holds a weak reference.
std::weak_ptr<SignalsLib::Signal<ResizeEvent>> resize_sink{resize_signal};

// Window system emits.
resize_signal->emit({new_width, new_height});

// Renderer consumes.
if (std::shared_ptr<SignalsLib::Signal<ResizeEvent>> signal{resize_sink.lock()}) {
    ResizeEvent ev;
    while (signal->consume(ev)) {
        handle_resize(ev.width, ev.height);
    }
}
```

### When to Use Signals vs Direct Calls

| Scenario | Mechanism |
|----------|-----------|
| Cross-system notification (resize, focus, damage) | `Signal<T>` |
| Same-tick, same-system data access | Direct component lookup |
| Parent-child ownership (device → swapchain) | RAII member variables |

---

## Dynamic Rendering

TronGrid uses **dynamic rendering** (`VK_KHR_dynamic_rendering`, core in Vulkan 1.3) instead of
traditional `VkRenderPass` and `VkFramebuffer` objects. This is the modern approach recommended by
the Vulkan tutorial and GPU vendors.

### What Changes

| Traditional (deprecated in TronGrid) | Dynamic rendering |
|--------------------------------------|-------------------|
| Pre-declare `VkRenderPass` with subpasses, attachment descriptions | No `VkRenderPass` object at all |
| Create `VkFramebuffer` per swapchain image | No `VkFramebuffer` object at all |
| `vkCmdBeginRenderPass` | `vkCmdBeginRendering` with `VkRenderingInfo` |
| Attachment references bound to subpass indices | Attachments specified inline at draw time |
| Pipeline creation needs render pass + subpass index | Pipeline creation uses `VkPipelineRenderingCreateInfo` |

### Why

- **Simpler** — no upfront declaration of pass structure, no framebuffer management
- **Flexible** — change attachments per-frame without recreating render passes
- **Rendergraph-friendly** — the graph decides attachments at compile time, passes them inline
- **Less boilerplate** — removing two entire Vulkan object types and their lifecycle code

### Usage Pattern

```cpp
vk::RenderingAttachmentInfo colour_attachment{};
colour_attachment.setImageView(*colour_view)
    .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
    .setLoadOp(vk::AttachmentLoadOp::eClear)
    .setStoreOp(vk::AttachmentStoreOp::eStore)
    .setClearValue(vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 1.0f}});

vk::RenderingInfo rendering_info{};
rendering_info.setRenderArea({{0, 0}, extent})
    .setLayerCount(1)
    .setColorAttachments(colour_attachment);

cmd.beginRendering(rendering_info);
// ... draw calls ...
cmd.endRendering();
```

### Pipeline Creation

Pipelines no longer reference a `VkRenderPass`. Instead, they declare their attachment formats
via `VkPipelineRenderingCreateInfo`:

```cpp
vk::PipelineRenderingCreateInfo rendering_create_info{};
rendering_create_info.setColorAttachmentFormats(colour_format)
    .setDepthAttachmentFormat(depth_format);

// Chain into pipeline create info
pipeline_info.setPNext(&rendering_create_info);
// No .setRenderPass() — it's gone
```

---

## Rendergraph *(target design)*

The render pipeline is orchestrated as a **directed acyclic graph (DAG)** of render passes.
Each pass declares its input and output resources. The rendergraph compiler:

1. **Topologically sorts** passes to determine execution order
2. **Inserts barriers** and layout transitions automatically
3. **Allocates transient resources** and aliases memory where lifetimes don't overlap
4. **Creates synchronisation** primitives (semaphores) between dependent passes

```text
┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│ Geometry  │────►│   Bloom  │────►│ Tonemap  │────►│ Present  │
│   Pass    │     │   Pass   │     │   Pass   │     │   Pass   │
└──────────┘     └──────────┘     └──────────┘     └──────────┘
      │                                                   │
      │           ┌──────────┐                            │
      └──────────►│ Offscreen│ (AI vision — same scene,   │
                  │   Pass   │  rendered to DLL buffer)   │
                  └──────────┘                            │
```

### Why a Rendergraph

- **No manual barrier management** — the graph knows which resources each pass reads/writes
- **Automatic resource lifetime** — transient textures exist only as long as needed
- **Easy to extend** — adding a new post-process pass is adding a node and edges
- **Correct by construction** — cycle detection at compile time, not mysterious GPU hangs

### Pass Registration

```cpp
rendergraph.add_resource("hdr_colour", vk::Format::eR16G16B16A16Sfloat, extent,
    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled);

rendergraph.add_pass("geometry",
    .inputs  = {},
    .outputs = {"hdr_colour", "depth"},
    .execute = [&](vk::raii::CommandBuffer& cmd) { /* draw scene */ }
);

rendergraph.add_pass("tonemap",
    .inputs  = {"hdr_colour"},
    .outputs = {"swapchain_image"},
    .execute = [&](vk::raii::CommandBuffer& cmd) { /* fullscreen quad */ }
);

rendergraph.compile(); // topological sort, barrier insertion, resource allocation
```

---

## Resource Handles *(target design)*

Engine-level resources (meshes, textures, shaders, materials) are managed through indirected
handles rather than raw pointers. A `ResourceHandle<T>` points into a `ResourceManager` by ID,
enabling:

- **Hot-reload** — swap the underlying resource without invalidating handles
- **Reference counting** — automatic unload when the last handle is released
- **Async loading** — handle is valid immediately, resource loads on a worker thread
- **Type safety** — `ResourceHandle<Texture>` can't be confused with `ResourceHandle<Mesh>`

```cpp
auto brick = resource_manager.load<Texture>("brick");   // returns ResourceHandle<Texture>
auto cube  = resource_manager.load<Mesh>("cube");       // returns ResourceHandle<Mesh>

// Handles remain valid across hot-reloads
if (brick) {
    bind_texture(brick.get());
}
```

---

## Platform Layer

The platform layer handles window creation and input. There is no abstraction layer — each platform
has its own code path:

- **Windows:** Win32 API (`CreateWindowEx`, message pump)
- **Linux:** XCB (`xcb_create_window`, event loop)

Platform-specific Vulkan surface creation uses `VK_USE_PLATFORM_WIN32_KHR` or `VK_USE_PLATFORM_XCB_KHR`.

---

## Build System

CMake 3.16+ with Ninja Multi-Config. Five presets cover all supported compiler/platform combinations:

| Preset | OS | Compiler |
|--------|----|----------|
| `windows-msvc` | Windows | MSVC (cl) |
| `windows-clang-cl` | Windows | Clang-CL (MSVC ABI) |
| `windows-mingw` | Windows | MinGW-w64 (GCC) |
| `linux-x11-gcc` | Linux | GCC |
| `linux-x11-clang` | Linux | Clang |

```bash
cmake --preset <name>
cmake --build build/<name> --config Debug
```

---

## Directory Structure

```text
tron_grid/
├── .claude/             ← project instructions
├── .github/             ← CI workflows, templates, Vulkan SDK actions
├── docs/                ← extended documentation (you are here)
├── libs/                ← internal static libraries (LEGO bricks)
├── src/                 ← main application source
├── CMakeLists.txt       ← root build configuration
├── CMakePresets.json    ← compiler/platform presets
├── .clang-format        ← code formatting rules
└── .editorconfig        ← editor settings
```

---

## Frame Synchronisation

The renderer uses double or triple buffering with fences and semaphores:

- **Image available semaphore** — signals when a swapchain image is ready for rendering
- **Render finished semaphore** — signals when rendering to the image is complete
- **In-flight fence** — prevents the CPU from submitting work for a frame that the GPU hasn't finished

---

## Reference: Vulkan Engine Architecture Tutorial

The architectural patterns in this document are informed by the official Vulkan tutorial series
on engine architecture. When implementing or extending any of these systems, consult the
relevant chapter:

| Topic | Tutorial URL |
|-------|-------------|
| Introduction & principles | <https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/01_introduction.html> |
| Architectural patterns (layered, DOD, service locator, components) | <https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/02_architectural_patterns.html> |
| Component systems (entity, lifecycle, type IDs) | <https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/03_component_systems.html> |
| Resource management (handles, async loading, hot-reload) | <https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/04_resource_management.html> |
| Rendering pipeline (culling, rendergraph, dynamic rendering) | <https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/05_render_pipeline.html> |
| Event systems | <https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/06_event_systems.html> |

---

## Future Architecture (Phases 2+)

Once the foundation is solid, the architecture will evolve towards:

- **GPU-driven rendering** — indirect draw calls, visibility culling on the GPU
- **Bindless resources** — all textures and buffers accessible via descriptor indexing
- **Mesh shading** — meshlet-based geometry processing, replacing the traditional vertex pipeline
- **Ray tracing** — acceleration structures for shadows and full global illumination
- **AI embodiment** — one AI brain per instance as a DLL/SO plugin with staged sensory protocol (see [AI_INTERFACE.md](AI_INTERFACE.md))
- **Off-screen rendering** — rendering to framebuffer for AI vision at Stage 2+ (Phase 9)
- **Multiplayer** — extract world state to a separate authoritative server, MMO networking (Phase 10)
