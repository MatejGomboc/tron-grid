# Vision

## The Idea

TronGrid is the renderer for a digital world where AI creatures perceive and navigate through rendered images.
The AI brain lives in a separate repository — TronGrid's job is to produce the frames that the AI sees.

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
┌─────────────┐       frames        ┌──────────────┐
│  TronGrid   │ ──────────────────> │   AI Brain   │
│  (renderer) │                     │ (separate    │
│             │ <────────────────── │    repo)     │
└─────────────┘    actions/state    └──────────────┘
```

TronGrid produces rendered frames. The AI brain consumes them, decides on actions, and sends state updates back.
The two communicate through a streaming API (Phase 9 on the roadmap).

## The World: A Tron-Inspired Cyberspace

The Grid is a digital realm inspired by Tron, rendered in stark neon against infinite darkness:

- **Neon geometry**: Glowing lines form the ground, walls, and structures. Cyan, magenta, orange — the classic palette
- **Data structures**: Towering constructs of light, pulsing data streams, geometric platforms
- **Programmes**: Conventional NPCs are geometric wireframe shapes — cubes, pyramids, polyhedra made of glowing
  lines. Patrol bots, guardian systems, data couriers — all following their coded routines. Simple geometry, simple minds
- **Users**: Human players appear as avatars, each with a unique visual signature

The aesthetic is not just visual — it informs everything:

- Energy instead of food
- Sectors instead of regions
- Derezzing instead of death
- The Grid instead of the world

| Aspect | Grid Equivalent |
|--------|-----------------|
| **NPCs** | Geometric wireframe shapes (simple programmes) |
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

## AI as Network Client

The AI brain runs as a **separate process** that connects to TronGrid via a streaming protocol —
just like a human player logging in. This gives complete separation with no FFI or shared memory.

```text
┌─────────────────────────────────────────────────────────────┐
│                    TRONGRID (C++20)                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │   Renderer   │  │   Physics    │  │  World State │      │
│  │  (Vulkan)    │  │              │  │              │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│                    ┌──────────────┐                         │
│                    │   Network    │◄────── Human Players    │
│                    │   Protocol   │◄────── AI Brain (bot)   │
│                    └──────────────┘                         │
└─────────────────────────────────────────────────────────────┘
                            ▲
                            │ TCP / UDP / WebSocket
                            │
┌───────────────────────────┴─────────────────────────────────┐
│              AI BRAIN (separate repository)                 │
│              Connects as just another client                │
└─────────────────────────────────────────────────────────────┘
```

TronGrid sends the AI extra data that human players do not receive:

- Rendered image from the AI's viewpoint (for visual perception)
- Precise sound events (not just an audio stream)
- Ground-truth entity data (for learning)

This is the same proven architecture that AAA studios use for MMO testing bots — automated clients
that connect via the standard player protocol. The difference is that those bots are disposable QA
tools, while this AI is a persistent entity with memory and personality that *lives* in the world.

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
