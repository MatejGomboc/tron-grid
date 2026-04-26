# Physically-Based Rendering

Technical reference for PBR in TronGrid. Covers microfacet theory, the
Cook-Torrance BRDF, material parameters, and HDR pipeline design.

> This document describes the rendering model used from Phase 6 onward.
> See `TODO.md` for the development journal and `ARCHITECTURE.md` for
> the engine design.

---

## Microfacet Theory

### The Model

At the microscopic scale, every surface is a landscape of tiny flat
mirrors — **microfacets** — each with its own surface normal. We never
model individual microfacets. Instead, we describe them statistically
using three functions:

- **Normal Distribution Function (NDF)** — the density of microfacets
  oriented in a given direction.
- **Masking-Shadowing Function (G)** — the fraction of microfacets
  visible from both the light and the viewer.
- **Fresnel Equations (F)** — the reflectance of each individual
  microfacet at a given angle.

This is a geometric optics model: each microfacet is larger than the
wavelength of light (no diffraction) but smaller than a pixel (so we
average statistically).

### The Normal Distribution Function

The NDF `D(m)` is a density of micro-area over the joint domain of
macro-area and solid angle. It answers the question: "How many
microfacets per unit macro-area point in direction m?"

The normalisation condition is:

```text
integral over hemisphere of D(m) * cos(theta_m) * d_omega = 1
```

The cosine weighting means tilted microfacets contribute less projected
area. This ensures the total projected microfacet area equals the
macro-surface area.

### Deriving the Cook-Torrance BRDF

For specular reflection, only microfacets whose normal equals the
**half-vector** `h = normalise(v + l)` can redirect light from the
light direction `l` to the view direction `v`.

Starting from the reflected flux through these microfacets and dividing
by the outgoing solid angle, the BRDF is:

```text
f_r(v, l) = D(h) * F(v . h) * G(v, l) / (4 * (n . v) * (n . l))
```

Where each factor comes from:

| Factor | Role |
|--------|------|
| `D(h)` | Density of microfacets oriented at the half-vector |
| `F(v . h)` | Fresnel reflectance of each microfacet |
| `G(v, l)` | Fraction of correctly-oriented microfacets that are not blocked |
| `4 * (n.v) * (n.l)` | Jacobian of the half-vector transformation + cosine normalisation |

The factor of 4 in the denominator comes from the relationship between
the solid angle of microfacet normals and the solid angle of outgoing
directions. When the outgoing direction is perturbed, the half-vector
changes by half that amount; the solid angle scales as the square of the
angular change, giving a factor of 1/4.

### Why the Geometry Function Matters

Without G, the BRDF diverges at grazing angles — `cos(theta)` approaches
zero in the denominator while D can remain large. The geometry function
compensates because at grazing angles most microfacets are occluded by
their neighbours.

G accounts for two phenomena:

- **Masking** — microfacets hidden from the viewer.
- **Shadowing** — microfacets hidden from the light.

### Energy Conservation and the Furnace Test

The **furnace test** verifies energy conservation: place a sphere in
uniform white illumination with a perfect reflector material (F = 1).
The sphere should be invisible against the background.

Single-scattering microfacet BRDFs fail this test at high roughness —
energy is lost to inter-reflections between microfacets. The loss can
reach 60% at roughness = 1.0 with GGX. The Kulla-Conty multi-scattering
compensation adds a lobe that recovers the missing energy.

---

## TronGrid BRDF Components

### GGX / Trowbridge-Reitz NDF

```text
D(h) = alpha^2 / (PI * ((n . h)^2 * (alpha^2 - 1) + 1)^2)
```

Where `alpha = roughness^2` (Disney reparametrisation for a perceptually
linear roughness slider).

**Why GGX?** It has a longer tail than Beckmann or Blinn-Phong, producing
the characteristic soft halo around specular highlights that matches
real-world measured data (MERL database). Beckmann highlights fall off
abruptly; GGX highlights have a bright core with a gradual fade.

**Numerically stable form** (Filament, avoids float16 catastrophic
cancellation when `n . h` is near 1):

```hlsl
float a = NdotH * roughness;
float k = roughness / (1.0 - NdotH * NdotH + a * a);
float D = k * k * (1.0 / PI);
```

### Height-Correlated Smith-GGX Visibility

The visibility form `V` folds the `1 / (4 * NdotV * NdotL)` denominator
into the geometry term:

```text
f_r = D * V * F
```

```hlsl
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
    float a2 = roughness * roughness;
    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / (GGXV + GGXL);
}
```

**Why height-correlated?** The separable (uncorrelated) Smith form
multiplies `G1(v) * G1(l)` independently, double-counting microfacets
that are both masked and shadowed. This makes rough surfaces too dark.
The correlated form is more physically accurate and only marginally
more expensive.

### Schlick Fresnel Approximation

```hlsl
float3 F_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
```

For dielectrics, `F0` is derived from the index of refraction:

```text
F0 = ((n - 1) / (n + 1))^2
```

**Accuracy:** Average error below 1% for dielectrics (IOR 1.0–2.2),
~1.5% for metals. 32x faster than the exact Fresnel equations.
Sufficient for all real-time use cases.

### Lambertian Diffuse

```hlsl
float3 Fd_Lambert(float3 albedo)
{
    return albedo / PI;
}
```

Scaled by `(1 - F) * (1 - metallic)` for energy conservation — energy
that goes to specular reflection should not also go to diffuse. Metals
have no diffuse component (metallic = 1 zeroes out diffuse entirely).

---

## Combined BRDF Evaluation

For a single light sample with radiance `L_c` and attenuation `a`:

```hlsl
float3 H = normalize(V + L);

float NdotV = abs(dot(N, V)) + 1e-5;
float NdotL = clamp(dot(N, L), 0.0, 1.0);
float NdotH = clamp(dot(N, H), 0.0, 1.0);
float VdotH = clamp(dot(V, H), 0.0, 1.0);

float roughness = max(perceptualRoughness * perceptualRoughness, MIN_ROUGHNESS);
float3 F0 = lerp(float3(0.04), albedo, metallic);

float  D = D_GGX(NdotH, roughness);
float  V = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
float3 F = F_Schlick(VdotH, F0);

float3 specular = D * V * F;
float3 diffuse  = (1.0 - F) * (1.0 - metallic) * albedo / PI;

float3 colour = (diffuse + specular) * L_c * a * NdotL;
```

---

## Numerical Stability

| Guard | Value | Purpose |
|-------|-------|---------|
| Min roughness (alpha) | 0.089 | Prevents NDF singularity and float16 denormals |
| NdotV epsilon | `abs(NdotV) + 1e-5` | Prevents division by zero in visibility |
| Fresnel clamp | `clamp(1 - cosTheta, 0, 1)` | Prevents `pow()` on negative values |
| Light min distance | `max(dist^2, 0.0001)` | Prevents inverse square singularity |
| PI | 3.14159265359 | Normalisation in NDF and diffuse |
| F0 dielectric default | 0.04 | Standard base reflectivity for non-metals |

---

## Obsidian Material Parameters

Obsidian is volcanic glass — an amorphous (non-crystalline) solid,
primarily SiO2. The isotropic microfacet model is a perfect fit because
there is no crystal lattice or grain structure.

| Parameter | Value | Notes |
|-----------|-------|-------|
| IOR | 1.486 – 1.50 | Singly refractive volcanic glass |
| F0 | 0.038 – 0.04 | Standard dielectric (`((n-1)/(n+1))^2`) |
| Metallic | 0.0 | Purely dielectric |
| Base colour (linear) | (0.005, 0.005, 0.01) | Deep black with subtle cool tint |
| Perceptual roughness | 0.03 – 0.15 | Polished obsidian; up to 0.5 for weathered |

TronGrid uses 0.15 to soften residual step-edge discontinuities in the
terraced terrain reflections (Phase 8 Etape 38). Pure polished values
(0.03 – 0.08) produce a glassier mirror look but amplify reflection
seams across terrace boundaries.

**Visual behaviour:** At normal incidence, obsidian appears nearly black
(F0 ~ 0.04, dark base colour = almost no reflected or diffuse light). At
grazing angles, Fresnel pushes reflectance toward 1.0, creating a bright
glossy sheen — the characteristic obsidian look.

---

## Neon Tube Dual-Colour Palette

The Tron aesthetic uses two neon colours: cyan as the primary grid colour
and orange as an accent on major grid lines. This matches the concept art
(`images/landscape_dark.png`, `images/landscape_light.png`) where both
colours appear in the floor grid.

| Colour | Linear RGB | Role |
|--------|-----------|------|
| Cyan | (0.0, 0.8, 1.0) | Primary grid — most cells |
| Orange | (1.0, 0.03, 0.0) | Accent — major grid lines (every 8th row/column). Low green survives HDR clamping pre-tonemapping. |

### Pattern Logic

The `neonEmissiveColour()` function in `mesh.slang` selects the emissive
colour based on the world-space position of the fragment:

```hlsl
float mod_x = fmod(floor(abs(world_pos.x)), MAJOR_GRID_SPACING);
float mod_z = fmod(floor(abs(world_pos.z)), MAJOR_GRID_SPACING);
bool is_orange = (mod_x < 0.5) || (mod_z < 0.5);
```

Cells whose X or Z coordinate falls on a multiple of `MAJOR_GRID_SPACING`
(8) get orange neon tubes. All other cells get cyan. This creates a coarse
orange supergrid overlay on top of the finer cyan grid — approximately 23%
of cells are orange. The `floor(abs(...))` order (rather than
`abs(floor(...))`) keeps the orange bands symmetric around the world
origin: without it, negative-x bands shift by one cell relative to
positive-x.

The same function is used for both primary fragments and reflection hit
points, ensuring reflected neon tubes display the correct colour.

---

## HDR Pipeline

### Why HDR?

Neon tube edges emit light at intensities well above 1.0 (e.g., 15.0).
A non-HDR (sRGB) colour attachment would clamp these values, making
emissive surfaces look flat-bright instead of blindingly intense. An HDR
framebuffer preserves the full dynamic range for:

- Accurate emissive material rendering.
- Bloom extraction (bright pixels above a threshold).
- Correct specular highlights from PBR (can exceed 1.0).

### Format

`VK_FORMAT_R16G16B16A16_SFLOAT` — 16-bit float per channel, 64 bits per
pixel. Sufficient for HDR; float32 is unnecessary bandwidth overhead.
Natively supported at full rate on all NVIDIA GPUs from Pascal onward.

### Rendering Pipeline

```text
 1. Transition MSAA colour: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL (transient)
 2. Transition HDR image:   UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
 3. Render opaque scene to MSAA → resolve into HDR (mesh shader pass)
 4. Render skybox into HDR (full-screen pass, depth test only)
 5. Render transparent scene into HDR (premultiplied-alpha blend, mesh
    shader pass with fragTransparent — see § Transparency and Refraction)
 6. Transition HDR + froxel grid + bloom mips + swapchain: → GENERAL
    (everything downstream is compute-shader storage I/O)
 7. Density-injection compute writes the froxel grid (extinction + radiance)
 8. Volumetric composite compute reads the froxel grid and blends fog
    into the HDR image — see § Volumetric Fog
 9. Bloom: extract bright pixels → mip chain downsample → tent-filter upsample
10. Post-process compute: composite bloom, apply ACES RRT+ODT, sRGB encode,
    write the swapchain image directly
11. Transition swapchain:   GENERAL → PRESENT_SRC_KHR
```

The volumetric composite is sequenced **before** bloom so the fog itself
can bloom — bright fog regions bleed correctly through the bloom chain
rather than appearing as sharp halos at slice boundaries.

The post-process compute shader (`postprocess.slang`) writes directly to
the swapchain via a storage image — no blit, no format-conversion hop.
The swapchain uses `VK_FORMAT_B8G8R8A8_UNORM` with
`VK_IMAGE_USAGE_STORAGE_BIT` so compute can write 8-bit post-tonemap
values while the shader performs exact IEC 61966-2-1 sRGB encoding.

### Pipeline Colour Format

The mesh shader pipeline's `VkPipelineRenderingCreateInfo` specifies
`R16G16B16A16_SFLOAT` as the colour attachment format for the MSAA +
HDR resolve pair, not the swapchain's 8-bit format.

---

## Tonemapping

The post-process compute pass (`postprocess.slang`) applies the ACES
**fitted RRT+ODT** with AP1 hue preservation, following the Hill / MJP
BakingLab implementation. This is a more accurate ACES output transform
than the Narkowicz analytic fit — it preserves the hue of saturated
emissives (notably orange neon, which the Narkowicz fit shifts toward
yellow).

Pipeline (HDR linear in, sRGB-encoded 8-bit out):

```text
HDR linear RGB (Rec.709)
  → transform to ACES AP1
  → per-channel RRT+ODT curve
  → transform back to Rec.709 (with AP1 hue-preservation correction)
  → clamp to [0, 1]
  → exact IEC 61966-2-1 sRGB encoding
```

Alternative fits considered and not used:

- **ACES Filmic (Narkowicz 2016)** — analytic, fast, but hue-shifts
  bright saturated colours toward white. Orange neon clamps to yellow.
- **Reinhard** — simple `c / (1 + c)`. No toe, no hue preservation;
  retained as a reference in the tonemapping literature.

---

## Transparency and Refraction

TronGrid renders transparent surfaces through a dedicated mesh shader pipeline
that shares its descriptor set layout, pipeline layout, and task / mesh stages
with the opaque pipeline. Only the fragment entry point, depth-write state,
and blend equations differ. Materials with `opacity < 1` route through the
transparent dispatch (a contiguous slice of the object SSBO selected via the
task shader's `base_object_index` push constant) and through `fragTransparent`
in `mesh.slang`.

### Snell's Law Refraction

The refraction direction follows Snell's law:

```text
n_1 · sin(θ_1) = n_2 · sin(θ_2)
```

The closed-form vector formula (HLSL `refract(I, N, eta)` and the GLSL spec
implement this exactly):

```text
k = 1 − η² · (1 − (N · I)²)
T = η · I − (η · (N · I) + √k) · N        if k ≥ 0
  = float3(0, 0, 0)                        if k < 0
```

Where:

| Symbol | Meaning |
|--------|---------|
| `I` | Incident direction (camera → surface) |
| `N` | Surface normal (oriented to face the incident ray) |
| `η` | Ratio of indices of refraction `n_from / n_to` |
| `k` | Discriminant — negative ⇒ total internal reflection (TIR) |
| `T` | Refracted direction |

The HLSL convention returns the zero vector on TIR; TronGrid's shader checks
`dot(T, T) < 1e-6` and falls back to `reflect(I, N)`.

For air → glass at IOR 1.5, `η = 1/1.5 ≈ 0.667`. For glass → air at the exit
boundary, `η = 1.5`; this is where TIR can occur (above the critical angle
`asin(1/1.5) ≈ 41.8°`).

### Fresnel for Glass

The same Schlick approximation used for opaque dielectric specular drives the
reflection / refraction split at a transparent surface:

```text
F = F0 + (1 − F0) · (1 − cos(θ_v))^5
F0 = ((n − 1) / (n + 1))²
```

For glass (IOR 1.5), `F0 ≈ 0.04`. At grazing angles `F → 1` so the surface
behaves as a mirror; at normal incidence `F ≈ 0.04` so the surface behaves as
a clear pane. The composite is:

```text
final = F · reflect_sample + (1 − F) · refract_sample · base_colour + emissive
```

The `base_colour` multiplier on the refracted contribution is a
**Beer-Lambert-lite** approximation — full Beer-Lambert attenuates by
`exp(-σ · d)` where `d` is the path length inside the medium, but for a thin
glass slab the linear tint is visually adequate and avoids requiring a second
ray hit to measure thickness.

### Ray-Traced Sample, Not Screen-Space Refraction

TronGrid traces an inline `RayQuery` along the refraction direction and
samples the hit material's `base_colour + emissive · emissive_strength`
directly (no recursive Cook-Torrance evaluation — that would require nested
ReSTIR sampling, prohibitively expensive). The reflection ray is traced the
same way. On miss, both rays return a low-intensity sky gradient.

A practical detail: a refraction ray fired from the front face of a glass box
would, by default, immediately re-hit the box's own back face and sample the
glass material again — defeating the entire purpose. The fix is to flag the
transparent BLAS geometries as **non-opaque** at acceleration-structure build
time. Inline `RayQuery` then reports each hit on these surfaces as a candidate
through `Proceed()`, letting the shader skip candidates whose
`CandidateInstanceID()` matches the originating instance. Other BLASes
(terrain, neon tubes, orb) keep `eOpaque` so their hits auto-commit at full
traversal speed.

### Premultiplied Alpha Blend

The transparent pipeline's blend state is configured for premultiplied alpha
"over" composition:

| Factor | Value |
|--------|-------|
| `srcColorBlendFactor` | `ONE` |
| `dstColorBlendFactor` | `ONE_MINUS_SRC_ALPHA` |
| `srcAlphaBlendFactor` | `ONE` |
| `dstAlphaBlendFactor` | `ONE_MINUS_SRC_ALPHA` |
| `colorBlendOp` / `alphaBlendOp` | `ADD` |

The shader outputs `(rgb · α, α)`; the blend equation then yields:

```text
final.rgb = src.rgb + (1 − src.α) · dst.rgb
```

Premultiplied (rather than non-premultiplied) is chosen so the composition
stays mathematically correct through MSAA resolve and any downstream
filtering — sampling a premultiplied texture through a bilinear filter
produces correct interpolated values, while sampling a non-premultiplied
texture introduces halo artefacts at edges.

The output α is intentionally less than 1 (currently 0.7) even though the
refraction ray query has explicitly sampled what's behind the surface. The
small destination bleed-through (30 %) softens the visual from "explicit
substitute" toward "tinted glass over a real background"; the slight
double-counting of the world behind the glass is accepted as a tuning
parameter rather than a strict-physics output.

### Why Not WBOIT?

The Etape 40 plan called out Weighted Blended Order-Independent Transparency
(McGuire & Bavoil 2013) as the canonical OIT technique. The actual
implementation deliberately uses simpler premultiplied-alpha blending
because the test scene contains only two non-overlapping transparent
entities — sorted alpha blending is equivalent to WBOIT for that case
without the accumulation + revealage render targets and composite compute
pass. WBOIT is queued for a future etape when the scene grows enough
overlapping translucent geometry to justify the plumbing cost.

---

## Volumetric Fog

TronGrid renders atmospheric scattering through a **frustum-aligned voxel
grid** (commonly called a *froxel grid*) — the AAA-standard approach
used by Frostbite, Unreal Engine 4/5, and Unity HDRP. The reference
paper is Wronski 2014 (SIGGRAPH "Volumetric Fog"); the temporal
reprojection follows the same pattern. Implementation lives in three
compute shaders:

- `src/inject_density.slang` — per-froxel emissive sampling, shadow ray,
  Henyey-Greenstein phase function.
- `src/volumetric_filter.slang` — spatial 3×3 XY blur + temporal
  reprojection (ping-pong).
- `src/volumetric_composite.slang` — per-pixel raymarch + composite onto
  HDR.

### The Froxel Grid

The grid is a 3D image (`R16G16B16A16_SFLOAT`, dimensions
`320 × 180 × 64`, ~30 MB) where each voxel — *froxel* — stores:

| Channel | Meaning |
|---------|---------|
| `.rgb` | In-scattered radiance toward the camera, pre-multiplied by `σ_s · slice_dz` (so the composite recurrence is just a load + multiply-add per slice) |
| `.a` | Optical depth = `σ_t · slice_dz` (extinction integrated over the slice's view-space depth). Drives the per-slice transmittance via `T_slice = exp(-.a)` in the composite |

X/Y are screen-space tiles (each froxel covers a 4×4-pixel screen region
at 1280×720). Z slices the view direction with a **logarithmic depth
distribution**:

```text
depth(z) = near · (far / near)^(z / Z)
```

where `near = 0.1 m`, `far = 120 m`, `Z = 64`. This packs more
resolution close to the camera (where the projected screen size of each
froxel is largest and parallax is most visible) and less at the far
plane.

The pipeline owns *three* such images: one raw inject target plus a
ping-pong pair for the temporal-reprojection filter. Total volumetric
VRAM ~90 MB.

### Scattering Equation

The volumetric rendering equation along a view ray, integrating from
camera (`s = 0`) to the surface hit (`s = D`):

```text
L_camera = ∫_0^D L_scatter(s) · T(0, s) ds + L_surface · T(0, D)
```

where the transmittance between two points is

```text
T(a, b) = exp(-∫_a^b σ_t(s') ds')
```

and `σ_t` is the extinction coefficient (absorption + out-scattering).
TronGrid models atmospheric haze with `σ_a ≈ 0`, so `σ_s = σ_t = density`.
For a discrete froxel grid the integral collapses to the classic
Frostbite recurrence:

```text
T_slice    = exp(-σ_t · dz)                     // transmittance through this slice
L_total   += L_slice · T_accumulated             // scattered light, attenuated by depth so far
T_accumulated *= T_slice                         // attenuate accumulated transmittance
```

After all slices, the final pixel colour is

```text
final = L_surface · T_accumulated + L_total
```

i.e. the surface colour attenuated by total transmittance through the
fog volume, plus all the light scattered from the volume back toward
the camera along the way.

### Density Injection

`inject_density.slang` runs once per froxel
(`[numthreads(8, 8, 1)]`, `(40 × 23 × 64)` workgroups).

For each froxel, the shader:

1. **Reconstructs the world-space position** of the froxel centre. NDC
   `xy` maps to a near-plane direction via `inv_view_projection`;
   `view_z` from the slice index is the depth along **camera_forward**,
   so the parametric distance along the ray is
   `t = view_z / dot(ray_dir, camera_forward)` — not `view_z` directly,
   which would put off-axis pixels at the wrong depth. `camera_forward`
   is recovered from `inv_view_projection` itself by unprojecting the
   NDC centre.
2. **Computes height-falloff density:** full strength at and below
   `FOG_HEIGHT_BASE = 4 m`, exponentially attenuated above with a
   `FOG_HEIGHT_FALLOFF = 6 m` scale. Models a ground-hugging fog layer
   that's denser near the Tron grid floor than up in the sky.
3. **Samples emissive geometry** (4 samples per froxel). Each sample uses
   the same power-weighted CDF that ReSTIR DI uses for the surface
   shader: `P(triangle i) = area_i · luminance_i / total_power`. Within
   a chosen triangle, sample uniformly in barycentric coordinates (Turk
   1990 / Shirley & Chiu).
4. **Traces a TLAS shadow ray** from the froxel centre toward each
   sample point with `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
   RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_CULL_NON_OPAQUE`. The
   `CULL_NON_OPAQUE` flag is load-bearing: glass and the energy-barrier
   pillar are flagged non-opaque (per Etape 40), and atmospheric light
   should pass through transparent surfaces rather than be blocked by
   them.
5. **Modulates by the Henyey-Greenstein phase function:**

   ```text
   phase(g, μ) = (1 - g²) / (4π · (1 + g² - 2g·μ)^1.5)
   ```

   where `μ = cos(view_dir, light_dir)` and `g = 0.6`. Forward-biased
   scatter — looking *toward* a tube through fog gives a brighter
   in-scatter than looking away. Reference: Henyey & Greenstein 1941;
   `g ≈ 0.6` is canonical for atmospheric haze.
6. **Combines** as `σ_s · dz · phase · (emissive · cos_light / dist²) /
   pdf · visibility` per sample. Per-sample firefly clamp (3.0) before
   averaging keeps single-sample spikes from dominating the mean.
7. **Writes** `(scattered, σ_t · dz)` into the raw inject image.

The `area` factor does NOT appear in the geometric kernel, despite
showing up in the CDF. The joint per-area sampling PDF is
`area_i · lum_i / total_power · 1/area_i = lum_i / total_power` (m⁻²)
— the area cancels exactly. Including it again would double-bias toward
large triangles.

### Spatial Blur + Temporal Reprojection (Filter Pass)

`volumetric_filter.slang` runs once per froxel and combines two
variance-reduction techniques in one compute pass:

**(1) Spatial 3×3 XY blur.** Per slice, each output froxel is the mean
of its 3×3 neighbourhood in the raw inject image. Reduces inter-froxel
variance (each froxel's 4 single-frame samples are independent — without
spatial averaging, adjacent froxels can land on wildly different "lucky
picks" from the 17 000-triangle CDF). Z is *not* blurred — the slice
integration is exact at slice boundaries; mixing slices would change
the integration model.

**(2) Temporal reprojection (Wronski 2014 / Frostbite).** Reconstruct
this froxel's world-space centre, project it through the **previous**
frame's view-projection matrix to find where the froxel sat last frame,
sample the previous frame's filter output trilinearly at the reprojected
coordinates, and EMA-blend with the current spatial-blurred value:

```text
out = lerp(history, current_blurred, alpha)
```

`alpha = 0.1` gives a ~10-frame effective sample count.

The Z reprojection inverts the logarithmic depth mapping:

```text
view_z   = prev_clip.w
t        = log(view_z / near) / log(far / near)
froxel_z = t · Z - 0.5
```

History is rejected if any of: `prev_clip.w ≤ 0` (behind previous
camera), reprojected XY outside the screen-aligned tile grid, or
reprojected Z outside `[0, Z)`. On rejection the filter outputs only the
spatial value (no temporal blend) — disocclusion handling.

A `history_valid` flag in the push constants is `0` on the very first
frame so the filter doesn't blend against an uninitialised history image.

**Ping-pong.** The filter both reads from one filter image (history) and
writes to the other (output). The host alternates the
(history, output) pair between the two physical filter images each
frame, so frame N+1's history is what frame N just wrote. This naturally
aligns with the existing `MAX_FRAMES_IN_FLIGHT = 2` ping-pong of
descriptor sets — filter set 0 reads B and writes A, filter set 1 reads A
and writes B.

**Layout preservation.** Filter ping-pong images get a one-shot startup
`UNDEFINED → GENERAL` transition; per-frame barriers thereafter use
`GENERAL → GENERAL` (rather than `UNDEFINED → GENERAL`) so the temporal
accumulator's contents survive across frames. Without this, the per-frame
discard would wipe the history every frame.

**Cross-submission visibility.** The previous submission's filter compute
write needs to be visible to this submission's filter compute read; per
the Vulkan memory model, submission ordering alone is not enough. A
compute-stage `MemoryBarrier2` at the top of `recordFrame` (alongside
the existing fragment-stage barrier for ReSTIR reservoirs) provides the
hand-off.

**Effective sample count per froxel:** 4 inject samples × 9 spatial
neighbours × ~10 temporal frames ≈ 360. Visually reads as soft
cyberpunk atmospheric haze rather than animated noise.

### Raymarch Composite

`volumetric_composite.slang` runs once per HDR pixel
(`[numthreads(8, 8, 1)]`, screen-tile dispatch). Reads from the
*filtered* froxel image (post spatial blur + temporal accumulation), not
the raw inject output.

For each pixel: walk through the 64 froxels of that pixel's column,
running the Frostbite recurrence above. Bilinear interpolation in XY
across the 320×180 grid smooths the upsample to screen resolution.
Output:

```text
hdr_image[pixel].rgb = scene · accumulated_transmittance + accumulated_radiance
```

The composite runs **before** bloom extraction so the fog itself can
bloom — bright fog regions (near the sun, around emissive geometry)
bleed correctly through the bloom chain rather than appearing as sharp
halos at slice boundaries.

### Why Compute, Not Pixel Shaders

The froxel grid is built once per frame on the GPU and consumed by the
composite. Doing density injection in a fragment shader (rasterising
proxy geometry) would be possible but ties the cost to screen fillrate.
With compute, each froxel pays a fixed cost regardless of camera angle.
At 320×180×64 = 3.7 M froxels with 4 ray queries each = ~15 M shadow
rays per frame, the inject runs in well under 2 ms on the RTX 4090.

### Vulkan Sequencing

Per-frame ordering inside `recordFrame`:

1. Cross-frame `MemoryBarrier2` at top: makes the previous submission's
   fragment-stage reservoir writes (ReSTIR) and compute-stage filter
   writes (volumetric history) visible to this frame's matching reads.
2. Mesh shader pass writes opaque + skybox + transparent HDR colour.
3. Image transitions: HDR + swapchain + bloom + raw-froxel + both filter
   ping-pong images → `eGeneral` (filter pair uses `GENERAL → GENERAL`
   to preserve history).
4. Density-injection compute writes the raw froxel image.
5. Memory barrier (compute → compute, raw-froxel write → filter read).
6. Filter compute: 3×3 spatial blur of raw inject + temporal-reprojection
   blend with previous frame's filter output → writes this frame's
   filter image.
7. Memory barrier (compute → compute, filter write → composite read).
8. Volumetric composite compute reads filter image + RW HDR.
9. Memory barrier (compute → compute, HDR write → bloom read).
10. Bloom extraction reads the **fogged** HDR.
11. Bloom downsample / upsample chain.
12. Tonemap composite to swapchain.

### Startup Warm-Up (Etape 42c-polish-2)

The filter pass's history image is empty at startup; without
intervention the first ~10 rendered frames show visible "confetti" MC
variance because only spatial smoothing is active until the temporal
accumulator builds up. To eliminate this warm-up phase, a one-shot
command buffer at renderer initialisation runs **16 inject + filter
cycles** with varying PCG seeds at the camera's initial pose, before
the main render loop opens:

- All three fog images (raw inject + both filter ping-pong) are
  transitioned to `eGeneral` first.
- Each cycle: `inject_push.frame_count = i` (varying seed for
  independent samples) → inject dispatch → barrier → filter dispatch
  with alternating ping-pong descriptor set (`i % 2`) → barrier.
- After the warm-up: the host bumps `frame_counter` to
  `WARMUP_ITERATIONS` so the first real frame's filter has
  `history_valid == 1`, and seeds `prev_view_projection` with the
  initial view-projection (rather than identity) so the first frame's
  reprojection lands on the same froxel-centre mapping the warm-up
  used.

Cost: ~50–70 ms one-shot, hidden in the application loading sequence.
Subsequent frames are uncompromised — the per-frame inject + filter
cost is identical with or without warm-up.

### Coordinate Convention Note (Y-flip)

The project's projection matrix is **math-convention** (+Y = up). The
mesh shader's `slangc -fvk-invert-y` flag flips `SV_Position` only at
the rasterisation stage; **compute shaders never see this flip**. So
inject's and filter's `froxelToWorld` must explicitly map froxel index
y=0 to math-NDC y=+1 (top of math frustum, which after the mesh
shader's flip becomes Vulkan top of screen) so the data inject writes
at froxel index y=0 displays at the screen position the composite
reads froxel index y=0 for. The straightforward
`ndc.y = (froxel_y + 0.5) / froxel_h * 2 - 1` formula does the
opposite (puts froxel index y=0 at math-NDC y=-1 = bottom of math
frustum) and produces vertically-mirrored fog data. Etape 42c-polish-2
fixed this with `ndc.y = 1 - uv.y * 2` in both shaders' `froxelToWorld`.
Bug was hidden since 41a by the orb being far above the camera and the
height-falloff curve being similar at two symmetric world heights.

### Limitations and Future Work

- **Density injection ignores the depth buffer** — fog accumulates
  through opaque geometry too. A depth-aware early-out (per-froxel
  visibility check against the depth buffer) would skip wholly-occluded
  froxels.
- **Per-slice scattering is "front of slice" approximation** — the
  Frostbite-accurate integrated form
  `L = (L · (1 - T_slice)) / σ_t` would attenuate scattered light
  through the slice itself, more accurate at high densities. Acceptable
  approximation while overall density is low.
- **Single-sample-per-froxel + 4-sample baseline could be replaced by
  RIS** (reservoir-style importance resampling like ReSTIR DI uses for
  surfaces). Would dramatically reduce variance per inject sample;
  larger architectural change. Phase 8 Etape 42 ("temporal denoising +
  adaptive LOD") is the natural place for it.
- **Bilinear upsample in composite is not edge-aware** — light shafts
  across surface boundaries can leak slightly. Bilateral upsample
  (depth-aware, normal-aware) would tighten this; also a Phase 8
  Etape 42 candidate.

---

## References

### PBR and Microfacet Theory

- [PBRT 4th Ed — Roughness Using Microfacet Theory](https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory)
- [Google Filament — PBR Material Model](https://google.github.io/filament/Filament.html)
- [LearnOpenGL — PBR Theory](https://learnopengl.com/PBR/Theory)
- [Heitz 2014 — Understanding the Masking-Shadowing Function](https://jcgt.org/published/0003/02/03/paper.pdf)
- [Walter et al. 2007 — Microfacet Models for Refraction (GGX)](https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf)
- [Burley 2012 — Physically Based Shading at Disney](https://media.disneyanimation.com/uploads/production_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf)
- [Kulla & Conty 2017 — Multi-Scattering Compensation](https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf)
- [Nathan Reed — How Is The NDF Really Defined?](https://www.reedbeta.com/blog/hows-the-ndf-really-defined/)

### Tonemapping and HDR

- [Narkowicz 2016 — ACES Filmic Tone Mapping Curve](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/)
- [Hill / MJP — ACES Fitted RRT+ODT (BakingLab)](https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl)
- [Google Filament — Tone Mapping (hue shift discussion)](https://google.github.io/filament/Filament.html#toneMapping)
- [Patry 2016 — HDR Colour Grading and Display in Frostbite (SIGGRAPH)](https://www.ea.com/frostbite/news/high-dynamic-range-color-grading-and-display-in-frostbite)
- [Hoffman 2017 — A Practical Approach to Colour in Games (SIGGRAPH PBR course)](https://blog.selfshadow.com/publications/s2017-shading-course/)
- [Hable 2010 — Filmic Tonemapping Operators (Uncharted 2)](http://filmicworlds.com/blog/filmic-tonemapping-operators/)
- [Sobotka — AgX Display Rendering Transform](https://github.com/sobotka/AgX)
- [Wrensch — Minimal AgX Implementation (Iolite)](https://iolite-engine.com/blog_posts/minimal_agx_implementation)
- [Stachowiak — Tony McMapface (hue-preserving 3D LUT)](https://github.com/h3r2tic/tony-mc-mapface)
- [64 — Tone Mapping (comprehensive comparison)](https://64.github.io/tonemapping/)
- [c0de517e — Tonemapping on HDR Displays](http://c0de517e.blogspot.com/2017/02/tonemapping-on-hdr-displays-aces-to.html)

### Bloom

- [LearnOpenGL — Physically Based Bloom](https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom)
- [LearnOpenGL — Bloom (classic two-pass Gaussian)](https://learnopengl.com/Advanced-Lighting/Bloom)
- [Catlike Coding — Bloom (mip chain downsample/upsample)](https://catlikecoding.com/unity/tutorials/advanced-rendering/bloom/)

### Ray Tracing and Light Sampling

- [Khronos — Ray Tracing Best Practices for Hybrid Rendering](https://www.khronos.org/blog/vulkan-ray-tracing-best-practices-for-hybrid-rendering)
- [NVIDIA — RTXDI (ReSTIR Direct Illumination)](https://developer.nvidia.com/rtx/ray-tracing/rtxdi)
- [NVIDIA Blog — Lighting with Millions of Lights Using RTXDI](https://developer.nvidia.com/blog/lighting-scenes-with-millions-of-lights-using-rtx-direct-illumination/)
- [Bitterli et al. — Spatiotemporal Reservoir Resampling (ReSTIR)](https://github.com/NVIDIA-RTX/RTXDI)
- [NVIDIA — Cyberpunk 2077 RT Overdrive Interview](https://www.nvidia.com/en-us/geforce/news/cyberpunk-2077-ray-tracing-overdrive-mode-interview/)
- [SIGGRAPH 2025 — Vulkan Ray Tracing with Dynamic Rendering](https://docs.vulkan.org/tutorial/latest/courses/18_Ray_tracing/00_Overview.html)

### Volumetric Fog

- [Wronski 2014 — Volumetric Fog (SIGGRAPH, froxel raymarching)](https://bartwronski.com/wp-content/uploads/2014/08/bwronski_volumetric_fog_siggraph2014.pdf)
- [diharaw — Volumetric Fog (OpenGL compute, frustum-aligned voxels)](https://github.com/diharaw/volumetric-fog)
- [Meteoros — Real-time Cloudscapes in Vulkan (Decima Engine)](https://github.com/AmanSachan1/Meteoros)

### Transparency

- [NVIDIA — 7 OIT Techniques in Vulkan](https://github.com/nvpro-samples/vk_order_independent_transparency)
- [Vulkan Docs — OIT with Per-Pixel Ordered Linked Lists](https://docs.vulkan.org/samples/latest/samples/api/oit_linked_lists/README.html)
- [LearnOpenGL — Weighted Blended OIT](https://learnopengl.com/Guest-Articles/2020/OIT/Weighted-Blended)
- [Tsopouridis 2024 — Deep and Fast Approximate OIT](https://onlinelibrary.wiley.com/doi/10.1111/cgf.15071)

### GPU Particle Systems

- [Vulkan Tutorial — Compute Shader (particle system)](https://vulkan-tutorial.com/Compute_Shader)
- [Sascha Willems — Vulkan Compute Particles](https://github.com/SaschaWillems/Vulkan/blob/master/examples/computeparticles/computeparticles.cpp)
- [Intel — Parallel Particle Systems Using Vulkan](https://www.intel.cn/content/dam/develop/external/us/en/documents/paralleltechniquesinmodelingparticlesystemsusingvulkanapi-754322.pdf)

### Nanite-Like Adaptive LOD

- [Epic — Nanite Virtualized Geometry (UE5 Docs)](https://dev.epicgames.com/documentation/unreal-engine/nanite-virtualized-geometry-in-unreal-engine)
- [Medium — Nanite: Epic's Practical Implementation](https://medium.com/@GroundZer0/nanite-epics-practical-implementation-of-virtualized-geometry-e6a9281e7f52)
- [jglrxavpok 2024 — Recreating Nanite: Mesh Shader Time](https://blog.jglrxavpok.eu/2024/05/13/recreating-nanite-mesh-shader-time.html)
- [Vulcanite — Nanite-like Vulkan Implementation](https://github.com/bdwhst/Vulcanite)

### Cinematic Post-Processing (CRT, Chromatic Aberration, Vignette)

- [GM Shaders — CRT Effect Breakdown](https://mini.gmshaders.com/p/gm-shaders-mini-crt)
- [Babylon.js — Retro CRT Shader Study](https://babylonjs.medium.com/retro-crt-shader-a-post-processing-effect-study-1cb3f783afbc)
- [Cyan — CRT Shader Breakdown](https://cyangamedev.wordpress.com/2020/09/10/retro-crt-shader-breakdown/)

### Procedural Sky and Clouds

- [keijiro — Volumetric Cloud Skybox Shader](https://github.com/keijiro/CloudSkybox)
- [shff — Pure-Shader Procedural Sky (OpenGL)](https://github.com/shff/opengl_sky)
- [Heckel — Real-time Cloudscapes with Volumetric Raymarching](https://blog.maximeheckel.com/posts/real-time-cloudscapes-with-volumetric-raymarching/)

### Light Trails and Glow

- [NVIDIA GPU Gems — Real-Time Glow](https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-21-real-time-glow)
- [Gamasutra — Real-Time Glow Technique](https://www.gamedeveloper.com/programming/real-time-glow)

### Industry Rendering Analyses

- [c0de517e — Cyberpunk 2077 Rendering Analysis](http://c0de517e.blogspot.com/2020/12/hallucinations-re-rendering-of.html)
- [zhangdoa — Cyberpunk 2077 Frame Analysis](https://zhangdoa.com/rendering-analysis-cyberpunk-2077/)
- [AMD GPUOpen — Porting Detroit: Become Human (Vulkan)](https://gpuopen.com/learn/porting-detroit-1/)
- [Quantic Dream — Detroit: A Vulkan in the Engine](https://blog.quanticdream.com/detroit-a-vulkan-in-the-engine/)
- [GMUNK — TRON: Legacy VFX](https://gmunk.com/TRON-Legacy)
- [Front Effects — TRON Legacy Digital World Breakdown](https://fronteffects.wordpress.com/2014/05/20/tron-legacy-2010-the-computer-world-of-tron-seen-28-years-later/)

---

*Last updated: 2026-04-26 (Etape 42c-polish-2 — fog Y-flip bug fix + warm-up pre-fill + density tuning)*
