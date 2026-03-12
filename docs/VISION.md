# Vision

## The Idea

TronGrid is the renderer for a digital world where AI creatures perceive and navigate through rendered images.
AI brains are DLL/SO plugins loaded by TronGrid client instances — each AI runs its own client,
just like a human player.

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
┌──────────────────┐                ┌──────────────────┐
│  TronGrid Server │◄── network ──►│  TronGrid Client  │
│  (authoritative) │               │  + AI Brain DLL   │
└──────────────────┘               └──────────────────┘
      separate repo                       THIS REPO
```

**This repository is the TronGrid client** — the application that players (human or AI) run on
their PCs. The authoritative MMORPG server is a separate project in its own repository.

Each player runs a TronGrid client that connects to the central server.
AI brains are DLL/SO plugins loaded by the client — the server sees all players identically.
See [AI Embodiment](#ai-embodiment) below for the full architecture.

## The World: A Tron-Inspired Cyberspace

The Grid is a digital realm inspired by Tron, rendered in stark neon against infinite darkness.
Three kinds of entity inhabit it:

- **Programmes** — simple NPCs. Geometric wireframe shapes — cubes, pyramids, polyhedra made of glowing lines.
  Patrol bots, guardian systems, data couriers — all following their coded routines. Simple geometry, simple minds
- **AI players** — persistent AI entities with memory and personality, each running their own
  TronGrid client with a brain DLL/SO plugin. Visually distinct from the geometric world
  (see [AI Visual Identity](#ai-visual-identity))
- **Human players** — real people logging in as avatars, each with a unique visual signature

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

There is **one TronGrid client binary**. At launch, the user chooses the client mode:

- **Human mode** — renders to screen, plays audio through speakers, reads keyboard/mouse input.
  This is a traditional MMORPG game client
- **Bot mode** — renders off-screen, routes all sensory output through the bot interface into
  an AI brain DLL/SO, reads actions back from it

The mode is a launch-time flag, not a compile-time difference. The same binary, the same
renderer, the same network protocol. The only thing that changes is where I/O goes.

```text
┌────────────────────────────────────────────────────────────────┐
│                    TRONGRID SERVER                              │
│  World State · Physics · Authority                             │
│  (sees only "clients" — cannot distinguish AI from human)      │
└──────┬──────────────────┬──────────────────┬───────────────────┘
       │                  │                  │
       │     Standard MMO protocol (identical for all clients)
       │                  │                  │
┌──────┴──────┐   ┌───────┴───────┐   ┌──────┴──────┐
│ Human Mode  │   │   Bot Mode   │   │  Bot Mode   │
│ (TronGrid)  │   │  (TronGrid)  │   │  (TronGrid) │
│             │   │              │   │             │
│ Screen → 👁 │   │  Offscreen → │   │ Offscreen → │
│ Speakers→ 👂│   │   DLL/SO     │   │  DLL/SO     │
│ Keyboard → ⌨│   │   brain      │   │  brain      │
│             │   │  Actions ←   │   │ Actions ←   │
└─────────────┘   └──────────────┘   └─────────────┘
```

| Aspect | Human mode | Bot mode |
|--------|-----------|----------|
| **Vision** | Rendered to screen → human eyes | Rendered off-screen → bot interface → DLL/SO |
| **Hearing** | Mixed to speakers → human ears | Spatial audio events → bot interface → DLL/SO |
| **Input** | Keyboard/mouse → actions | DLL/SO → bot interface → actions |
| **To server** | Identical action packets | Identical action packets |
| **Launch** | `trongrid` | `trongrid --bot brain.dll` |

### AI Brains as Independent Shared Libraries

The AI brain is a **DLL** (Windows) or **SO** (Linux) — an **independent project**, built and
maintained entirely outside of TronGrid. The brain's internal architecture is completely up to
its author: neural network, rule-based system, LLM wrapper, hand-coded state machine — anything
goes. TronGrid does not care what is inside the DLL. It only cares about the interface.

TronGrid publishes a **standardised bot interface** — a shared memory contract through which the
client and the brain exchange sensory data and actions. The brain never links against TronGrid
internals, never calls engine functions, and never touches world state. It reads from a sensory
buffer and writes to an action buffer. That is the entire relationship.

```text
┌──────────────────────────┐       ┌──────────────────────────┐
│   TronGrid Client        │       │   AI Brain (DLL/SO)      │
│   (bot mode)             │       │   (independent project)  │
│                          │       │                          │
│  Renders off-screen ─────────►   │  Reads sensory buffer    │
│  Samples world state     │ bot   │                          │
│                          │ iface │  (user's own code —      │
│  Reads action buffer ◄─────────  │   any architecture,      │
│  Sends to server         │       │   any language, any AI)  │
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

### Build Your Own Bot

Anyone can build an AI bot for TronGrid. The requirements are:

1. **Produce a DLL/SO** that implements the standardised bot interface
2. **Read sensory data** from the shared memory buffer TronGrid provides
3. **Write actions** to the shared memory buffer TronGrid reads

Everything else — the language, the framework, the AI approach, the training pipeline — is
entirely up to the bot author. TronGrid is agnostic.

### Multiple AI Players

Each AI player runs its own TronGrid client instance in bot mode with its own DLL/SO loaded —
exactly as if multiple human players each launched their own copy of the game. Different AI
brains can use different DLLs (different species, different strategies). From the server's
perspective, they are all just players.

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
| 9 | AI integration | Frame streaming API for AI brain |

Phases 3–4 and 5–6 can be developed in parallel after Phase 2.

```text
Phase 0 --> 1 --> 2 --+--> 3 --> 4
                      |
                      +--> 5 --> 6

3 + 6 --> 7 --> 8 --> 9
```
