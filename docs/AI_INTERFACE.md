# AI Interface Protocol

> **Design principle:** The AI brain is a DLL/SO plugin. It receives only what its senses
> provide — never raw world state. The TronGrid client calls into the brain each tick;
> the brain returns actions. The server cannot tell AI clients from human clients.

The AI brain is a **shared library** (`.dll` on Windows, `.so` on Linux) loaded by a standard
TronGrid client instance. The client renders off-screen, samples the world, and passes structured
sensory data to the brain through a C ABI function interface. The brain returns motor actions.
The client then sends those actions to the server using the same network protocol as every other
player.

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

## Brain DLL/SO Interface

The AI brain exports a small set of C-linkage functions. The TronGrid client loads the library
at startup and calls these functions at the appropriate lifecycle points.

### Required Exports

```c
/// Called once when the brain's entity spawns into the world.
/// The brain receives arena geometry and its own initial state.
void on_spawn(const SpawnInfo* info);

/// Called every simulation tick.
/// The client fills `senses` with the current sensory data;
/// the brain fills `actions` with its desired motor output.
void on_tick(const SensoryPacket* senses, ActionPacket* actions);

/// Called when the entity is derezzed (destroyed).
/// The brain should clean up any internal state.
void on_derez(void);
```

### Optional Exports

```c
/// Called immediately after the library is loaded, before on_spawn.
/// Returns the protocol stage the brain supports (0, 1, 2, or 3).
/// If not exported, the client assumes Stage 0.
uint8_t get_protocol_stage(void);

/// Called when the client wants to hot-reload the brain.
/// The brain should serialise its internal state into the provided buffer.
/// Returns the number of bytes written, or 0 if hot-reload is not supported.
uint32_t serialise_state(void* buffer, uint32_t buffer_size);

/// Called after hot-reload, passing the previously serialised state.
void deserialise_state(const void* buffer, uint32_t size);
```

### Loading Sequence

```text
1. Client starts up, connects to server, receives spawn position
2. Client loads brain DLL/SO (path from command line or config)
3. Client calls get_protocol_stage() → determines which sensory subsystems to enable
4. Client calls on_spawn() with arena geometry and initial state
5. Main loop:
   a. Client receives world update from server
   b. Client samples world at entity's position → fills SensoryPacket
   c. Client calls on_tick(&senses, &actions)
   d. Client sends actions to server via standard network protocol
6. On derez: client calls on_derez()
7. On shutdown or hot-reload: client calls serialise_state() if exported
```

---

## Stage 0: Scalar Sensory Channels

At Stage 0, the client samples the world at the AI's position and delivers scalar readings.
No rendering pipeline is involved — just field sampling and collision queries.

### Client → Brain: `SensoryPacket`

```c
#define MAX_SIGNATURE_CHANNELS 8

/// What the TronGrid client passes to the brain each tick.
/// The client performs all world sampling; the brain never sees world state directly.
struct SensoryPacket
{
    /// Simulation tick counter (monotonically increasing).
    uint64_t tick;
    /// Timestep duration in seconds.
    double dt_seconds;

    // ── Energy / Data Signature Channels ─────────────────────────
    /// Readings from distinct energy or data signature types.
    /// Each channel represents a different detectable substance in the Grid
    /// (energy traces, programme signatures, data stream residue, etc.).
    struct SignatureReading signature_channels[MAX_SIGNATURE_CHANNELS];
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
struct SignatureReading
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

**Channel semantics:** Each `SignatureReading` represents a distinct detectable substance or
energy type in the Grid. Initial implementation uses a single channel (generic energy signature).
Future stages add programme-type signatures, data stream traces, player residue, and sector
identity markers.

### Brain → Client: `ActionPacket`

```c
/// What the brain returns each tick.
/// Stage 0 motor output: forward/backward movement and steering.
struct ActionPacket
{
    /// Forward velocity (m/s).
    /// Positive = forward movement, negative = retreat.
    double forward_velocity;
    /// Angular velocity (rad/s).
    /// Positive = anticlockwise (left turn).
    double angular_velocity;
};
```

Two doubles. At later stages, `ActionPacket` extends with interaction targets, look direction,
and emote signals — but Stage 0 needs only locomotion.

### Spawn Info

```c
/// Passed to on_spawn() when the entity enters the world.
struct SpawnInfo
{
    /// Protocol stage the client is operating at.
    uint8_t stage;
    /// Tick rate in Hz (e.g. 1000 for 1 ms timestep).
    uint32_t tick_rate_hz;
    /// Number of active signature channels.
    uint8_t num_signature_channels;
    /// Arena geometry.
    struct ArenaDescriptor arena;
};

/// Arena geometry descriptor.
struct ArenaDescriptor
{
    enum Shape { ARENA_CIRCLE, ARENA_RECTANGLE, ARENA_SECTOR };
    enum Shape shape;
    double dimension_a;  // radius (circle) or width (rectangle) in metres
    double dimension_b;  // unused (circle) or height (rectangle) in metres
};
```

---

## What the Client Computes

The TronGrid client's responsibility is **sensory simulation** — sampling its local copy of the
world state at the AI's position. At Stage 0, this involves:

| Sensory channel | Client computation |
|----------------|-------------------:|
| `SignatureReading.intensity` | Sample energy/signature field at the AI's sensing position |
| `SignatureReading.d_intensity_dt` | Temporal difference: `(intensity_now - intensity_prev) / dt` |
| `SignatureReading.lateral_gradient` | Cross product of field gradient with heading vector |
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
it renders the scene from the AI entity's viewpoint into a framebuffer and passes the pixel data
directly to the brain — zero-copy via pointer.

```c
/// Extended SensoryPacket fields at Stage 2+.
/// These are appended to the base Stage 0 packet.
struct VisionData
{
    /// Pointer to the rendered RGBA8 framebuffer.
    /// Valid only for the duration of the on_tick call.
    const uint8_t* pixels;
    /// Frame dimensions.
    uint32_t width;
    uint32_t height;
    /// Horizontal field of view in radians.
    double fov_horizontal;
};

struct AudioEvent
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

struct AudioData
{
    /// Array of spatial audio events this tick.
    const struct AudioEvent* events;
    /// Number of audio events.
    uint32_t num_events;
};
```

The vision buffer is rendered by the same Vulkan pipeline that renders for human players — the
only difference is that it targets an off-screen framebuffer instead of the swapchain. The AI
sees exactly what a human would see from the same position.

---

## Determinism and Replay

The protocol supports **deterministic replay** by design:

1. **Record:** Log every `SensoryPacket` with its tick number during a session.
2. **Replay:** Feed the logged packets to the brain DLL with the same initial state.
3. **Verify:** The output `ActionPacket` sequence must be identical.

This is essential for debugging AI behaviour and for the training pipeline. The clean function
call boundary makes replay trivial — just call `on_tick` with recorded data.

---

## Protocol Evolution Summary

| Aspect | Stage 0 (Scalar) | Stage 2 (Visual) | Stage 3 (Full) |
|--------|-----------------|-------------------|----------------|
| **SensoryPacket size** | ~120 bytes | +frame buffer (~32 MB at 4K) | +full audio stream |
| **ActionPacket size** | 16 bytes (2 doubles) | ~32 bytes (+look direction) | ~64 bytes (+targets, emotes) |
| **Client rendering** | None required | Off-screen Vulkan render | Full pipeline |
| **Transfer method** | Struct copy | Zero-copy pointer | Zero-copy pointer |
| **Visual data** | `light_level` scalar only | RGBA framebuffer | RGBA + depth |
| **Audio data** | None | Spatial audio events | Full spatial audio |

The protocol starts minimal and grows only as the brain's capabilities demand. The client
enables more subsystems at each stage; the server never changes — it always sees just another
player.

---

*See also: [VISION.md](VISION.md) | [ARCHITECTURE.md](ARCHITECTURE.md)*
