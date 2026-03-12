# AI Interface Protocol

> **Design principle:** The AI brain is an independent DLL/SO — a separate project with its own
> architecture. TronGrid defines only the shared memory interface through which sensory data and
> actions are exchanged. The brain's internals are entirely up to its author.

## Overview

The AI brain is a **shared library** (`.dll` on Windows, `.so` on Linux) — an independent
project built entirely outside of TronGrid. When the TronGrid client is launched in **bot mode**
(`trongrid --bot brain.dll`), it loads the brain library, renders off-screen, and routes all
sensory output through the bot interface. The client and the brain communicate through **shared
memory buffers** — the client writes sensory data, the brain reads it; the brain writes actions,
the client reads them. The brain never links against TronGrid, never calls engine functions,
and never sees raw world state.

The brain's internal architecture is **completely agnostic** — neural network, rule-based,
hand-coded, LLM-driven, or anything else. TronGrid does not impose any AI framework, language
constraint, or design pattern. The only requirement is: implement the interface, produce a
DLL/SO.

For the overall architecture, see [VISION.md](VISION.md) § [AI Embodiment](VISION.md#ai-embodiment).

---

## Why a Staged Protocol?

The [VISION.md](VISION.md) describes a rich sensory stream: rendered frames, spatial audio, energy
signatures, collision feedback, temperature fields. Implementing all of this at once would be
premature — the AI brain's cognitive capabilities develop incrementally, and the protocol should
match.

A Stage 0 AI brain navigates by sensing energy gradients, contact events, and ambient conditions —
it does not yet process visual frames or audio spectrograms. Passing rendered images to a brain that
cannot interpret them wastes GPU cycles and adds complexity for no benefit.

The protocol therefore grows in stages:

| Stage | AI capability | Sensory scope |
|-------|--------------|---------------|
| **0** | **Gradient navigation, reflexes** | **Scalar sensory channels (this document)** |
| 1 | Spatial awareness, threat response | Low-resolution directional sensing |
| 2 | Visual recognition, audio processing | Off-screen rendered frames, spatial audio |
| 3 | Full cognitive autonomy | Complete sensory stream from [VISION.md](VISION.md) |

Each stage **extends** the previous — no existing channels are removed. Stage 0 validation tests
remain valid at every subsequent stage.

---

## Shared Memory Interface

The TronGrid client and the AI brain communicate through shared memory regions. The client owns
and manages these regions; the brain receives access to them when loaded.

```text
┌──────────────────────────────────────────────────┐
│                Shared Memory Layout              │
│                                                  │
│  ┌────────────────────┐  ┌────────────────────┐  │
│  │   Sensory Buffer   │  │   Action Buffer    │  │
│  │   (client writes,  │  │   (brain writes,   │  │
│  │    brain reads)    │  │    client reads)   │  │
│  └────────────────────┘  └────────────────────┘  │
│                                                  │
│  ┌────────────────────┐                          │
│  │   Control Block    │                          │
│  │   (handshake,      │                          │
│  │    tick sync,      │                          │
│  │    protocol stage) │                          │
│  └────────────────────┘                          │
└──────────────────────────────────────────────────┘
```

The brain DLL/SO exports a minimal set of C-linkage functions that the client calls to pass
shared memory pointers and lifecycle events. These are the **only** functions the brain needs
to export — everything else is internal to the brain.

### Required Exports

```c
/// Called once after loading. The brain fills `config` with its requirements
/// (e.g. requested eye resolution), then receives the shared memory interface.
void tg_brain_init(const TgBrainInterface* interface, TgBrainConfig* config);

/// Called once when the brain's entity spawns into the world.
void tg_brain_spawn(const TgSpawnInfo* info);

/// Called every simulation tick.
/// The brain reads from the sensory buffer and writes to the action buffer
/// (both accessible through the interface passed in tg_brain_init).
void tg_brain_tick(void);

/// Called when the entity is derezzed (destroyed).
/// The brain should clean up any internal state.
void tg_brain_shutdown(void);
```

### Brain Configuration

The brain fills this struct during `tg_brain_init` to tell the client what it needs.
The client reads it after the call and configures its subsystems accordingly.

```c
/// Filled by the brain during tg_brain_init.
/// Tells the client what sensory capabilities the brain requires.
struct TgBrainConfig
{
    /// Protocol stage the brain supports (0 = scalar, 1 = directional, 2 = visual, 3 = full).
    uint8_t stage;

    /// Requested eye resolution for Stage 2+ vision.
    /// Ignored at Stage 0/1. The client renders offscreen at this resolution.
    uint32_t eye_width;
    uint32_t eye_height;

    /// Requested tick rate in Hz (e.g. 60 for 60 ticks/second).
    /// The client will attempt to match this but may deliver a different rate.
    uint32_t requested_tick_rate_hz;
};
```

### Interface Structure

```c
/// Passed to the brain at init. Contains pointers to the shared memory regions
/// and metadata about the interface. The brain stores this and uses it each tick.
struct TgBrainInterface
{
    /// Protocol version (major, minor, patch).
    uint8_t version[3];

    /// Actual protocol stage the client is operating at.
    uint8_t stage;

    /// Actual tick rate the client is providing (may differ from requested).
    uint32_t tick_rate_hz;

    /// Pointer to the sensory buffer (client writes, brain reads).
    /// The layout depends on the protocol stage.
    const void* sensory_buffer;

    /// Size of the sensory buffer in bytes.
    uint32_t sensory_buffer_size;

    /// Pointer to the action buffer (brain writes, client reads).
    void* action_buffer;

    /// Size of the action buffer in bytes.
    uint32_t action_buffer_size;
};
```

### Lifecycle

```text
1. User launches: trongrid --bot brain.dll
2. Client starts in bot mode, connects to server, receives spawn position
3. Client loads brain DLL/SO from the specified path
4. Client calls tg_brain_init() → brain fills TgBrainConfig (stage, eye resolution, tick rate)
5. Client reads config, allocates shared memory, configures offscreen rendering at requested resolution
6. Client calls tg_brain_spawn() with arena geometry and initial state
7. Main loop:
   a. Client receives world update from server
   b. Client samples world at entity's position → writes into sensory buffer
   c. At Stage 2+: client renders offscreen, reads back to shared memory (triple-buffered)
   d. Client calls tg_brain_tick()
   e. Client reads actions from action buffer → sends to server
8. On derez: client calls tg_brain_shutdown()
9. Client unloads DLL/SO
```

---

## Stage 0: Scalar Sensory Channels

At Stage 0, the client samples the world at the AI's position and writes scalar readings into
the sensory buffer. No rendering pipeline is involved — just field sampling and collision queries.

### Sensory Buffer Layout (Stage 0)

```c
#define TG_MAX_SIGNATURE_CHANNELS 8

/// Sensory buffer layout at Stage 0.
/// The client writes this each tick; the brain reads it.
struct TgSensoryBuffer
{
    /// Simulation tick counter (monotonically increasing).
    uint64_t tick;
    /// Timestep duration in seconds.
    double dt_seconds;

    // ── Energy / Data Signature Channels ─────────────────────────
    /// Readings from distinct energy or data signature types.
    /// Each channel represents a different detectable substance in the Grid
    /// (energy traces, programme signatures, data stream residue, etc.).
    struct TgSignatureReading signature_channels[TG_MAX_SIGNATURE_CHANNELS];
    /// Number of active signature channels this tick.
    uint8_t num_signature_channels;

    // ── Contact / Collision ──────────────────────────────────────
    /// Front-facing contact intensity [0.0, 1.0].
    double contact_front;
    /// Rear contact intensity [0.0, 1.0].
    double contact_rear;
    /// Lateral body contact (brushing surfaces, other entities) [0.0, 1.0].
    double contact_lateral;

    // ── Energy Field (Temperature Analogue) ──────────────────────
    /// Ambient energy level at position (warmth near sources, cold in void).
    double energy_field;
    /// Rate of energy field change (dE/dt, per second).
    double d_energy_dt;

    // ── Body State (Proprioception) ──────────────────────────────
    /// Current body curvature (normalised, 0.0 = neutral posture).
    double body_curvature;
    /// Current forward speed (metres/s).
    double speed;
    /// Body heading (radians, world frame).
    double heading;

    // ── Damage (Pain Analogue) ───────────────────────────────────
    /// Damage intensity [0.0, 1.0]. 0.0 = safe, 1.0 = critical.
    double damage;

    // ── Ambient Light ────────────────────────────────────────────
    /// Light level at position [0.0, 1.0]. Scalar only — no direction, no image.
    double light_level;
};

/// A single signature channel reading.
struct TgSignatureReading
{
    /// Signature intensity at the AI's sensing position (non-negative).
    double intensity;
    /// Temporal derivative of intensity (dI/dt, per second).
    /// Positive = intensity increasing (approaching source).
    double d_intensity_dt;
    /// Lateral gradient component (cross-body difference).
    /// Positive = higher intensity on the left side.
    /// Used by the brain for steering during forward movement.
    double lateral_gradient;
};
```

**Channel semantics:** Each `TgSignatureReading` represents a distinct detectable substance or
energy type in the Grid. Initial implementation uses a single channel (generic energy signature).
Future stages add programme-type signatures, data stream traces, player residue, and sector
identity markers.

### Action Buffer Layout (Stage 0)

```c
/// Action buffer layout at Stage 0.
/// The brain writes this each tick; the client reads it.
struct TgActionBuffer
{
    /// Forward velocity (m/s).
    /// Positive = forward movement, negative = retreat.
    double forward_velocity;
    /// Angular velocity (rad/s).
    /// Positive = anticlockwise (left turn).
    double angular_velocity;
};
```

Two doubles. At later stages, the action buffer extends with interaction targets, look direction,
and emote signals — but Stage 0 needs only locomotion.

### Spawn Info

```c
/// Passed to tg_brain_spawn() when the entity enters the world.
struct TgSpawnInfo
{
    /// Tick rate in Hz (e.g. 1000 for 1 ms timestep).
    uint32_t tick_rate_hz;
    /// Number of active signature channels.
    uint8_t num_signature_channels;
    /// Arena geometry.
    struct TgArenaDescriptor arena;
};

/// Arena geometry descriptor.
struct TgArenaDescriptor
{
    enum TgArenaShape { TG_ARENA_CIRCLE, TG_ARENA_RECTANGLE, TG_ARENA_SECTOR };
    enum TgArenaShape shape;
    double dimension_a;  // radius (circle) or width (rectangle) in metres
    double dimension_b;  // unused (circle) or height (rectangle) in metres
};
```

---

## What the Client Computes

The TronGrid client's responsibility is **sensory simulation** — sampling its local copy of the
world state at the AI's position and writing results into the shared sensory buffer. At Stage 0,
this involves:

| Sensory channel | Client computation |
|----------------|-------------------:|
| `TgSignatureReading.intensity` | Sample energy/signature field at the AI's sensing position |
| `TgSignatureReading.d_intensity_dt` | Temporal difference: `(intensity_now - intensity_prev) / dt` |
| `TgSignatureReading.lateral_gradient` | Cross product of field gradient with heading vector |
| `contact_front` / `contact_rear` | Physics collision detection at body endpoints |
| `contact_lateral` | Lateral collision along body extent |
| `energy_field` / `d_energy_dt` | Sample ambient energy field at position |
| `damage` | Accumulate damage from hostile programmes, energy depletion, hazards |
| `light_level` | Sample ambient light at position |
| `body_curvature` / `speed` / `heading` | Read from physics body state |

All operations are scalar field samples or collision queries. **No rendering pipeline is required
at Stage 0.**

### Energy / Signature Field Implementation

TronGrid maintains scalar fields that the AI can sense. The simplest useful model:

```c
/// A point source contributing to a signature field.
struct SignatureSource
{
    Vec3 position;
    double intensity;    // peak intensity at source
    double radius;       // falloff radius
    double falloff;      // Gaussian sigma or inverse-square parameter
    uint8_t channel;     // which signature channel this contributes to
};
```

Concentration at a point is the sum of all nearby sources' contributions (Gaussian falloff or
inverse-square — configurable per source type). Gradients are computed analytically or by
finite difference.

---

## Stage 2+: Vision and Audio

At Stage 2, the client enables its Vulkan rendering pipeline in **off-screen mode**. Each tick,
it renders the scene from the AI entity's viewpoint into an offscreen framebuffer and writes the
pixel data into the shared sensory buffer.

### Brain-Requested Resolution

The brain specifies its desired eye resolution at init time — **not** the engine. Different
brains need different visual acuity. A primitive creature might need 8x8; a visually sophisticated
one might request 256x256. TronGrid renders the offscreen pass at whatever resolution the brain
asks for.

| Resolution | Per-frame size (RGB + depth) | Use case |
|-----------|----------------------------|----------|
| 8 x 8 | ~0.4 KB | Light/dark sensing, phototaxis |
| 32 x 32 | ~7 KB | Basic spatial awareness |
| 64 x 64 | ~28 KB | Moderate visual recognition |
| 128 x 128 | ~112 KB | Detailed visual processing |
| 256 x 256 | ~448 KB | High-acuity vision |

There is no reason to render at 4K for a brain that processes 64x64. The resolution is part of
the brain's configuration, communicated during `tg_brain_init`.

### Vision Data

```c
/// Extended sensory buffer fields at Stage 2+.
/// These are appended to the base Stage 0 layout.
struct TgVisionData
{
    /// Pointer to the rendered RGB888 framebuffer within the shared memory region.
    /// Row-major, top-to-bottom.
    const uint8_t* rgb;
    /// Pointer to the depth buffer (normalised [0.0, 1.0], float32).
    /// Row-major, same dimensions as RGB.
    const float* depth;
    /// Frame dimensions (as requested by the brain at init).
    uint32_t width;
    uint32_t height;
    /// Horizontal field of view in radians.
    double fov_horizontal;
};
```

### GPU Readback Pipeline

The client renders to a GPU-only `VkImage`, copies it to a staging buffer, and the brain reads
from persistently mapped CPU-visible memory. Triple-buffered to avoid stalls:

```text
GPU-only VkImage ──► vkCmdCopyImageToBuffer ──► Staging buffer (CPU-visible, persistently mapped)
                                                        │
                                                        ▼
                                                  Brain reads from
                                                  shared sensory buffer
```

The readback runs 2 frames behind rendering — at 60 FPS that is ~33 ms latency, comparable to
biological visual processing delays. At small resolutions (64x64 = 28 KB) the bandwidth is
trivial.

### Audio Data

```c
struct TgAudioEvent
{
    /// Bearing to the sound source relative to entity heading (radians).
    double bearing;
    /// Distance to the sound source (metres).
    double distance;
    /// Intensity [0.0, 1.0].
    double intensity;
    /// Sound category identifier.
    uint32_t category;
};

struct TgAudioData
{
    /// Array of spatial audio events this tick.
    const struct TgAudioEvent* events;
    /// Number of audio events.
    uint32_t num_events;
};
```

### Visual Fidelity

The vision buffer is rendered by the same Vulkan pipeline that renders for human players — the
only difference is that it targets an offscreen framebuffer at the brain's requested resolution
instead of the swapchain. The AI sees exactly what a human would see from the same position,
just at a different pixel count.

---

## Building a Bot

To create an AI bot for TronGrid:

1. **Include the interface header** — TronGrid will publish a standalone `tg_brain_interface.h`
   containing all struct definitions and export function signatures. This header has no
   dependencies on TronGrid internals
2. **Implement the four exported functions** — `tg_brain_init`, `tg_brain_spawn`,
   `tg_brain_tick`, `tg_brain_shutdown`
3. **Build as a DLL/SO** — use any language that can produce a shared library with C-linkage
   exports (C, C++, Rust, Zig, etc.)
4. **Drop it next to the TronGrid client** — specify the path at launch

The bot's internal architecture is entirely up to you. TronGrid does not care.

---

## Determinism and Replay

The protocol supports **deterministic replay** by design:

1. **Record:** Log every sensory buffer snapshot with its tick number during a session.
2. **Replay:** Feed the logged snapshots to the brain DLL with the same initial state.
3. **Verify:** The output action buffer sequence must be identical.

This is essential for debugging AI behaviour and for the training pipeline.

---

## Protocol Evolution Summary

| Aspect | Stage 0 (Scalar) | Stage 2 (Visual) | Stage 3 (Full) |
|--------|-----------------|-------------------|----------------|
| **Sensory buffer size** | ~120 bytes | +frame buffer (brain-requested resolution) | +full audio stream |
| **Action buffer size** | 16 bytes (2 doubles) | ~32 bytes (+look direction) | ~64 bytes (+targets, emotes) |
| **Client rendering** | None required | Off-screen Vulkan render at brain's resolution | Full pipeline |
| **Transfer method** | Shared memory write | GPU readback → shared memory | GPU readback → shared memory |
| **Visual data** | `light_level` scalar only | RGB + depth at requested resolution | RGB + depth |
| **Audio data** | None | Spatial audio events | Full spatial audio |

The protocol starts minimal and grows only as the brain's capabilities demand. The client
enables more subsystems at each stage; the server never changes — it always sees just another
player.

---

*See also: [VISION.md](VISION.md) | [ARCHITECTURE.md](ARCHITECTURE.md)*
