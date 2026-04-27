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

// Shared procedural-sky sampling — Slang module imported by both the dedicated skybox
// pipeline (skybox.slang, primary-ray sky pixels) and the mesh-shader fragment
// (mesh.slang, RT reflection rays that miss geometry on the obsidian floor and need to
// pick up the sky's contribution to the reflection).
//
// Compiled with `import skybox_common;` from the consumer's translation unit. slangc
// finds this file via `-I <src dir>` (configured in src/CMakeLists.txt). The module is
// never compiled to a stand-alone SPV — it has no entry points; only its `public`
// functions and constants are exposed to importers.
//
// Visibility: per Slang's module rules, the default access level for module-scope
// declarations is `internal` (visible only within the module). Only `sampleProceduralSky`
// is `public` since that's the single entry point importers need; the noise helpers and
// constants stay internal because external callers should not depend on them.

module skybox_common;

// ── Internal value-noise helpers ──
//
// Same algorithm and seed pattern as the one originally inlined in skybox.slang
// (which was itself ported from terrain.cpp). Centralising it here keeps the visual
// signature of the sky identical when it is sampled from primary rays vs reflection
// rays — any future tuning of these helpers updates both code paths in lockstep.

//! Hash function — produces a pseudo-random value in [0, 1] from integer coordinates.
float hashNoise(int2 p, uint seed)
{
    uint h = uint(p.x) * 374761393u + uint(p.y) * 668265263u + seed * 1274126177u;
    h = (h ^ (h >> 13)) * 1103515245u;
    h = h ^ (h >> 16);
    return float(h & 0x7FFFFFFF) / float(0x7FFFFFFF);
}

//! 2D value noise with smoothstep interpolation.
float valueNoise(float2 coord, uint seed)
{
    int2 i = int2(floor(coord));
    float2 f = frac(coord);

    // Smoothstep interpolation.
    f = f * f * (3.0 - 2.0 * f);

    float v00 = hashNoise(i + int2(0, 0), seed);
    float v10 = hashNoise(i + int2(1, 0), seed);
    float v01 = hashNoise(i + int2(0, 1), seed);
    float v11 = hashNoise(i + int2(1, 1), seed);

    float v0 = lerp(v00, v10, f.x);
    float v1 = lerp(v01, v11, f.x);
    return lerp(v0, v1, f.y);
}

//! Layered value noise (octaves) for cloud density.
float layeredNoise(float2 coord, uint octaves, float frequency, uint seed)
{
    float value = 0.0;
    float amplitude = 1.0;
    float total_amplitude = 0.0;
    float freq = frequency;

    for (uint i = 0; i < octaves; ++i) {
        value += amplitude * valueNoise(coord * freq, seed + i);
        total_amplitude += amplitude;
        amplitude *= 0.5;
        freq *= 2.0;
    }

    return value / total_amplitude;
}

// ── Sky constants ──

static const float3 SKY_BASE_COLOUR = float3(0.002, 0.005, 0.004);
static const float3 CLOUD_COLOUR = float3(0.0, 0.04, 0.03);
static const float CLOUD_FREQUENCY = 0.8;
static const uint CLOUD_OCTAVES = 3;
static const uint CLOUD_SEED = 42;
static const float CLOUD_SPEED = 0.01;
static const float CLOUD_DENSITY_THRESHOLD = 0.45;
static const float CLOUD_DENSITY_SOFTNESS = 0.2;

//! Samples the procedural cyberpunk sky for a given world-space ray direction at a given
//! time. Returns linear HDR radiance (no tonemapping applied here).
//!
//! \param ray_dir World-space ray direction. Caller is responsible for normalisation.
//! \param time    Animation time in seconds (drives the cloud-drift scroll).
//! \return Linear-space radiance for the sky at this direction.
//!
//! Used by:
//!   - skybox.slang fragment shader for primary rays (sky pixels at depth = 1).
//!   - mesh.slang fragment shader for reflection rays that miss all scene geometry —
//!     without this the obsidian floor's reflection looked black wherever the reflected
//!     ray pointed at the sky, leaving the orb / neon tubes as isolated bright shapes
//!     against pitch-black instead of being framed by the warm horizon glow and
//!     cloud wisps.
public float3 sampleProceduralSky(float3 ray_dir, float time)
{
    // Horizon density gradient — clouds concentrate at the horizon, sparse overhead.
    float horizon_factor = 1.0 - abs(ray_dir.y);
    horizon_factor = horizon_factor * horizon_factor;

    // Sample layered value noise on the ray direction (XZ plane) with time scroll.
    float2 noise_coord = ray_dir.xz / max(abs(ray_dir.y) + 0.1, 0.1);
    noise_coord += float2(time * CLOUD_SPEED, time * CLOUD_SPEED * 0.7);

    float density = layeredNoise(noise_coord, CLOUD_OCTAVES, CLOUD_FREQUENCY, CLOUD_SEED);

    // Shape the density with a soft threshold.
    density = smoothstep(CLOUD_DENSITY_THRESHOLD, CLOUD_DENSITY_THRESHOLD + CLOUD_DENSITY_SOFTNESS, density);
    density *= horizon_factor;

    // Blend between dark sky base and brighter cloud wisps.
    return lerp(SKY_BASE_COLOUR, CLOUD_COLOUR, density);
}
