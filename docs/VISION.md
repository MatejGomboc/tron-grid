# Vision

## The Idea

TronGrid is a single-player game engine and renderer for a digital world where an AI creature
perceives and navigates through rendered images. A single AI brain can be plugged in as a DLL/SO
plugin — one brain per TronGrid instance.

Think of it as building a terrarium, but digital: a self-contained world with its own geometry, lighting, and physics,
observed entirely through the lens of a GPU-driven renderer.

## Why Build a Custom Renderer?

Off-the-shelf engines (Unreal, Unity, Godot) optimise for human players. TronGrid optimises for a different consumer: an AI model
that needs consistent, high-fidelity frames streamed at low latency. This means:

- **No editor, no GUI** — the renderer is headless-capable, driven by API calls
- **Deterministic rendering** — same inputs produce same outputs, essential for training
- **Frame streaming** — frames are captured and piped to the AI brain, not displayed for a human
- **Full control** — every pipeline stage is ours to inspect, modify, and optimise

## Architecture at a Glance

```text
┌─────────────────────────────────┐
│           TronGrid              │
│                                 │
│  ┌───────────┐  ┌────────────┐  │
│  │ Renderer   │  │ Local World │  │
│  │ Input      │◄►│ State      │  │
│  │ AI brain   │  │ (in-process)│  │
│  └───────────┘  └────────────┘  │
└─────────────────────────────────┘
```

TronGrid is currently a **single-player engine**. The world state lives in-process. One AI brain
(DLL/SO) can be loaded per instance via the bot interface.
See [AI Embodiment](#ai-embodiment) below and [Future: Multiplayer](#future-multiplayer) for the
long-term MMO vision.

## The World: A Tron-Inspired Cyberspace

The Grid is a digital realm inspired by Tron, rendered in stark neon against infinite darkness.
Three kinds of entity inhabit it:

- **Programmes** — simple NPCs. Geometric wireframe shapes — cubes, pyramids, polyhedra made of glowing lines.
  Patrol bots, guardian systems, data couriers — all following their coded routines. Simple geometry, simple minds
- **The AI** — a persistent AI entity with memory and personality, loaded as a DLL/SO brain plugin.
  Visually distinct from the geometric world (see [AI Visual Identity](#ai-visual-identity))
- **The player** — a human controlling an avatar via keyboard/mouse, or absent in bot-only mode

The world itself is built from:

- **Neon geometry**: Glowing lines form the ground, walls, and structures. Cyan, magenta, orange — the classic palette
- **Data structures**: Towering constructs of light, pulsing data streams, geometric platforms

The aesthetic is not just visual — it informs everything:

- Energy instead of food
- Sectors instead of regions
- Derezzing instead of death
- The Grid instead of the world

| Aspect | Grid Equivalent |
|--------|-----------------|
| **Mortality** | Programmes derez permanently; players resurrect at a safe house |

### Rendering Requirements

The Tron aesthetic demands specific rendering capabilities:

| Effect | Implementation |
|--------|----------------|
| **Neon glow** | Multi-pass bloom, HDR rendering |
| **Grid lines** | Procedural geometry, anti-aliased lines |
| **Data streams** | Particle systems, animated UVs |
| **Reflections** | Screen-space reflections on grid floor |
| **Light trails** | Motion blur, temporal effects |

### Colour Palette

| Colour | Hex | Role |
|--------|-----|------|
| Cyan | `#00FFFF` | Primary |
| Magenta | `#FF00FF` | Accent |
| Orange | `#FF8800` | Warning / energy |
| White | `#FFFFFF` | Highlights |
| Black | `#000000` | Void background |

### AI Visual Identity

The AI should look *different* from the geometric world around it:

- **Organic curves** in a world of straight lines
- **Soft glow** versus harsh neon
- **Warm colour** (soft white/gold) versus cool cyan
- **Pulsing** brightness that reflects emotional state

## AI Embodiment

There is **one TronGrid binary**. At launch, the user chooses the mode:

- **Human mode** — renders to screen, plays audio through speakers, reads keyboard/mouse input.
  A traditional single-player game experience
- **Bot mode** — renders off-screen, routes all sensory output through the bot interface into
  a single AI brain DLL/SO, reads actions back from it

The mode is a launch-time flag, not a compile-time difference. The same binary, the same
renderer, the same world. The only thing that changes is where I/O goes.

```text
┌──────────────────────────────────────────────────────────────┐
│                       TronGrid                                │
│              ┌──────────────────────┐                         │
│              │   Local World State  │                         │
│              │   Physics · NPCs     │                         │
│              └──────────┬───────────┘                         │
│                         │                                     │
│          ┌──────────────┴──────────────┐                      │
│          │                             │                      │
│   ┌──────┴──────┐              ┌───────┴──────┐               │
│   │ Human Mode  │              │  Bot Mode    │               │
│   │             │              │              │               │
│   │ Screen → 👁 │              │ Offscreen →  │               │
│   │ Speakers→ 👂│              │  DLL/SO brain│               │
│   │ Keyboard → ⌨│              │ Actions ←    │               │
│   └─────────────┘              └──────────────┘               │
└──────────────────────────────────────────────────────────────┘
```

| Aspect | Human mode | Bot mode |
|--------|-----------|----------|
| **Vision** | Rendered to screen → human eyes | Rendered off-screen → bot interface → DLL/SO |
| **Hearing** | Mixed to speakers → human ears | Spatial audio events → bot interface → DLL/SO |
| **Input** | Keyboard/mouse → actions | DLL/SO → bot interface → actions |
| **Launch** | `trongrid` | `trongrid --bot brain.dll` |

### AI Brains as Independent Shared Libraries

The AI brain is a **DLL** (Windows) or **SO** (Linux) — an **independent project**, built and
maintained entirely outside of TronGrid. The brain's internal architecture is completely up to
its author: neural network, rule-based system, LLM wrapper, hand-coded state machine — anything
goes. TronGrid does not care what is inside the DLL. It only cares about the interface.

TronGrid publishes a **standardised bot interface** — a shared memory contract through which the
engine and the brain exchange sensory data and actions. The brain never links against TronGrid
internals, never calls engine functions, and never touches world state. It reads from a sensory
buffer and writes to an action buffer. That is the entire relationship.

```text
┌──────────────────────────┐       ┌──────────────────────────┐
│   TronGrid               │       │   AI Brain (DLL/SO)      │
│   (bot mode)             │       │   (independent project)  │
│                          │       │                          │
│  Renders off-screen ─────────►   │  Reads sensory buffer    │
│  Samples world state     │ bot   │                          │
│                          │ iface │  (user's own code —      │
│  Reads action buffer ◄─────────  │   any architecture,      │
│                          │       │   any language, any AI)  │
└──────────────────────────┘       └──────────────────────────┘
```

The sensory data provided through the interface:

- **Vision** — off-screen rendered RGBA frame from the creature's viewpoint
- **Hearing** — spatial audio events with source direction and distance
- **Smell** — energy signature gradients (detecting programmes, data streams, players nearby)
- **Touch** — contact and collision feedback from surfaces and entities
- **Temperature** — ambient energy field (warmth near sources, cold in the void)
- **Pain** — damage intensity from attacks or energy depletion

No ground truth. No entity positions. No cheating. The AI knows only what it can sense — just
like a human player knows only what they can see and hear. This is honest embodiment.

### Engine Coupling

Inside TronGrid, the 3D rendering engine, 3D physics engine, and 3D spatial audio engine are
**tightly coupled** — they share the same Vulkan device, GPU buffers, and compute queues for
maximum efficiency. The AI brain is the opposite: **loosely coupled** via the shared memory bot
interface, with no access to engine internals. See [ARCHITECTURE.md](ARCHITECTURE.md) § Coupling
Model for details.

### Build Your Own Bot

Anyone can build an AI bot for TronGrid. The requirements are:

1. **Produce a DLL/SO** that implements the standardised bot interface
2. **Read sensory data** from the shared memory buffer TronGrid provides
3. **Write actions** to the shared memory buffer TronGrid reads

Everything else — the language, the framework, the AI approach, the training pipeline — is
entirely up to the bot author. TronGrid is agnostic.

For the detailed bot interface specification (shared memory layout, staged protocol, packet
structures), see [AI_INTERFACE.md](AI_INTERFACE.md).

## Target Hardware

- **GPU:** NVIDIA RTX 4090 (Ada Lovelace), 16 GB VRAM
- **Resolution:** 4K (3840 x 2160)
- **Frame rate:** 60+ FPS
- **Rendering:** Full ray tracing

## Design Principles

1. **Don't over-engineer.** No abstractions until there's a concrete second use case.
2. **GPU-driven.** Let the GPU make decisions — bindless descriptors, indirect draws, mesh shaders.
3. **Physically based.** Linear colour space, real-world units (metres), correct light transport.
4. **Low latency.** MAILBOX present mode, minimal CPU-GPU synchronisation stalls.
5. **Platform parity.** Windows (Win32) and Linux (X11) are first-class citizens. No macOS, no mobile.

## Future: Multiplayer

The long-term vision is an MMORPG where multiple human and AI players share a persistent world.
The architecture is designed so that the transition from single-player to multiplayer is clean:

1. **Extract world state** from in-process to a separate authoritative server (its own repository)
2. **Replace direct calls** with network packets (standard MMO client-server protocol)
3. **Add prediction + reconciliation** on the client side

```text
┌──────────────────┐                ┌──────────────────┐
│  TronGrid Server │◄── network ──►│  TronGrid Client  │
│  (authoritative) │               │  + AI Brain DLL   │
└──────────────────┘               └──────────────────┘
    future repo                          THIS REPO
```

In this future model, each player (human or AI) runs their own TronGrid instance. Multiple AI
brains with different DLLs (different species, strategies) all appear as ordinary players. The
server cannot distinguish AI from human — the bot interface and network protocol remain identical.

Nothing about the bot interface, the renderer, or the AI brain DLL changes when multiplayer is
added. The only thing that changes is where world state lives.

## Phased Roadmap

The project is built incrementally. Each phase produces a working, demonstrable result.
The canonical task checklist lives in `TODO.md`.

| Phase | Goal | Milestone |
|-------|------|-----------|
| 0 | Prove the toolchain | Triangle on screen |
| 1 | Solid foundation | Fly through cubes |
| 2 | GPU-driven resources | 1000 objects, 1 draw call |
| 3 | Mesh shaders | Meshlet rendering |
| 4 | Procedural geometry | Tron-style CSG scene |
| 5 | Acceleration structures | Hard shadows |
| 6 | Physically-based RT | Full RT lighting |
| 7 | Post processing | Bloom, tonemapping |
| 8 | Optimisation | 4K @ 60+ rock-solid |
| 9 | AI integration | Bot interface, single AI brain per instance |
| 10 | Multiplayer | Authoritative server, MMO networking |

Phases 3–4 and 5–6 can be developed in parallel after Phase 2.

```text
Phase 0 --> 1 --> 2 --+--> 3 --> 4
                      |
                      +--> 5 --> 6

3 + 6 --> 7 --> 8 --> 9 --> 10
```
