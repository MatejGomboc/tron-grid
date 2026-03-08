# AI Interface Protocol

> **Design principle:** The AI client defines what sensory data it needs.
> TronGrid adapts its world sampling to match — not the other way around.

TronGrid uses a **traditional MMO architecture**: one authoritative server, each player runs their
own local client instance. AI brains connect to a **headless TronGrid client** running on the same
machine — the server sees all clients identically and has no AI-specific code.

This document specifies the **local sensory protocol** between a headless TronGrid client and an
AI brain process. This is not the server protocol — it runs locally on the AI's machine. For the
overall architecture, see [VISION.md](VISION.md) §
[AI as Network Client](VISION.md#ai-as-network-client).

---

## Why a Staged Protocol?

The [VISION.md](VISION.md) describes a rich sensory stream: rendered frames, spatial audio, energy
signatures, collision feedback, temperature fields. Implementing all of this at once would be
premature — the AI brain's cognitive capabilities develop incrementally, and the protocol should
match.

A Stage 0 AI brain navigates by sensing energy gradients, contact events, and ambient conditions —
it does not yet process visual frames or audio spectrograms. Sending rendered images to a brain that
cannot interpret them wastes bandwidth and adds complexity for no benefit.

The protocol therefore grows in stages:

| Stage | AI capability | Protocol scope |
|-------|--------------|----------------|
| **0** | **Gradient navigation, reflexes** | **Scalar sensory channels (this document)** |
| 1 | Spatial awareness, threat response | Low-resolution directional sensing |
| 2 | Visual recognition, audio processing | Rendered frames, FFT spectrograms |
| 3 | Full cognitive autonomy | Complete sensory stream from [VISION.md](VISION.md) |

Each stage **extends** the previous — no existing channels are removed. Stage 0 validation tests
remain valid at every subsequent stage.

---

## Stage 0: Scalar Sensory Channels

At Stage 0, TronGrid samples the world at the AI client's position and delivers scalar readings.
No rendering pipeline is involved — just field sampling and collision queries.

### Headless Client → AI Brain: `SensoryPacket`

```cpp
/// What TronGrid sends the AI client each simulation tick.
/// The server performs all world sampling; the AI never sees world state directly.
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
    SignatureReading signature_channels[MAX_SIGNATURE_CHANNELS];
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
    /// Used by the AI for steering during forward movement.
    double lateral_gradient;
};
```

**Channel semantics:** Each `SignatureReading` represents a distinct detectable substance or
energy type in the Grid. Initial implementation uses a single channel (generic energy signature).
Future stages add programme-type signatures, data stream traces, player residue, and sector
identity markers.

### AI Brain → Headless Client: `ActionPacket`

```cpp
/// What the AI client sends back each tick.
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

---

## What the Headless Client Computes (Client-Side)

The headless client's responsibility is **sensory simulation** — sampling its local copy of the
world state at the AI's position. At Stage 0, this involves:

| Sensory channel | Server computation |
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
at Stage 0.** The headless client receives world state from the server (energy sources, data
structures, environmental fields) and samples them locally at the AI's position.

### Energy / Signature Field Implementation

TronGrid maintains scalar fields that the AI can sense. The simplest useful model:

```cpp
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

## Handshake

Before streaming packets, the AI brain and headless client perform a local handshake:

```cpp
/// AI brain → Headless client: connection request.
struct ClientHandshake
{
    /// Protocol version (major, minor, patch).
    uint8_t version[3];
    /// Protocol stage (0 = scalar channels, 1 = directional, 2 = visual, 3 = full).
    uint8_t stage;
    /// Requested tick rate in Hz (e.g. 1000 for 1 ms timestep).
    uint32_t tick_rate_hz;
    /// Number of signature channels the brain expects.
    uint8_t num_signature_channels;
};

/// Headless client → AI brain: connection accepted.
struct ServerHandshake
{
    /// Actual tick rate the server will provide.
    uint32_t tick_rate_hz;
    /// Number of signature channels the server supports.
    uint8_t num_signature_channels;
    /// Arena geometry.
    ArenaDescriptor arena;
};

/// Arena geometry descriptor.
struct ArenaDescriptor
{
    enum class Shape : uint8_t { Circle, Rectangle, Sector };
    Shape shape;
    double dimension_a;  // radius (circle) or width (rectangle) in metres
    double dimension_b;  // unused (circle) or height (rectangle) in metres
};
```

The `stage` field in the handshake tells the headless client which sensory subsystems to enable.
A Stage 0 AI never receives rendered frames; a Stage 2 AI receives everything a Stage 0 AI does,
plus visual data (rendered offscreen by the headless client's Vulkan pipeline).

---

## Wire Format

| Option | Pros | Cons | Recommendation |
|--------|------|------|----------------|
| **Raw struct** | Minimal overhead, zero-copy | Version-sensitive, alignment issues | Local development |
| **MessagePack** | Compact binary, schema-flexible | Small parsing cost | Production candidate |
| **FlatBuffers** | Zero-copy, schema evolution, C++ native | Build complexity | Production alternative |
| **JSON** | Human-readable, debuggable | Verbose, slow | Debug/inspection only |

**Packet size estimate:** `SensoryPacket` with 1 signature channel is approximately **120 bytes**.
At 1 kHz tick rate: ~120 KB/s — trivially within any transport's capacity.

**Transport:** TCP for development (reliable, ordered). UDP with sequence numbers for production
latency requirements. At 120 bytes per packet, fragmentation is never an issue.

---

## Determinism and Replay

The protocol supports **deterministic replay** by design:

1. **Record:** Log every `SensoryPacket` with its tick number during a session.
2. **Replay:** Feed the logged packets to the AI brain with the same initial state.
3. **Verify:** The output `ActionPacket` sequence must be identical.

This is essential for debugging AI behaviour and for the training pipeline. The protocol's clean
packet boundary makes replay possible across process boundaries and even across machines.

---

## Protocol Evolution Summary

| Aspect | Stage 0 (Scalar) | Stage 3 (Full) |
|--------|-----------------|----------------|
| **SensoryPacket size** | ~120 bytes | ~50 KB+ (rendered frame) |
| **ActionPacket size** | 16 bytes (2 doubles) | ~64 bytes (vectors, targets, emotes) |
| **Client rendering** | None required | Offscreen Vulkan render (not displayed) |
| **Visual processing** | None | Rendered frame for AI vision |
| **Audio processing** | None | Spatial audio events / FFT |
| **Entity awareness** | None (field sampling only) | Entity list with bearings, types, states |

The protocol starts minimal and grows only as the AI brain's capabilities demand. The headless
client enables more subsystems at each stage; the server never changes.

---

*See also: [VISION.md](VISION.md) | [ARCHITECTURE.md](ARCHITECTURE.md)*
