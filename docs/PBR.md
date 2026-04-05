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

For a single point light with colour `L_c` and attenuation `a`:

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
| Perceptual roughness | 0.03 – 0.08 | Polished obsidian; up to 0.5 for weathered |

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
float mod_x = fmod(abs(floor(world_pos.x)), MAJOR_GRID_SPACING);
float mod_z = fmod(abs(floor(world_pos.z)), MAJOR_GRID_SPACING);
bool is_orange = (mod_x < 0.5) || (mod_z < 0.5);
```

Cells whose X or Z coordinate falls on a multiple of `MAJOR_GRID_SPACING`
(8) get orange neon tubes. All other cells get cyan. This creates a coarse
orange supergrid overlay on top of the finer cyan grid — approximately 23%
of cells are orange.

The same function is used for both primary fragments and reflection hit
points, ensuring reflected neon tubes display the correct colour.

---

## HDR Pipeline

### Why HDR?

Neon tube edges emit light at intensities well above 1.0 (e.g., 15.0).
The current SRGB swapchain clamps these values, making emissive surfaces
look flat-bright instead of blindingly intense. An HDR framebuffer
preserves the full dynamic range for:

- Accurate emissive material rendering.
- Bloom extraction in Phase 7 (bright pixels above a threshold).
- Correct specular highlights from PBR (can exceed 1.0).

### Format

`VK_FORMAT_R16G16B16A16_SFLOAT` — 16-bit float per channel, 64 bits per
pixel. Sufficient for HDR; float32 is unnecessary bandwidth overhead.
Natively supported at full rate on all NVIDIA GPUs from Pascal onward.

### Rendering Pipeline

```text
1. Transition HDR image: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
2. Transition swapchain: UNDEFINED → TRANSFER_DST_OPTIMAL
3. Render scene to HDR image (mesh shader pass)
4. Transition HDR image: COLOR_ATTACHMENT_OPTIMAL → TRANSFER_SRC_OPTIMAL
5. Blit HDR image → swapchain image (format conversion, clamping)
6. Transition swapchain: TRANSFER_DST_OPTIMAL → PRESENT_SRC_KHR
```

`vkCmdBlitImage` handles the `R16G16B16A16_SFLOAT` → `B8G8R8A8_SRGB`
format conversion. Values above 1.0 are clamped during the blit. A
tonemapping pass (ACES Filmic or Reinhard) will replace the clamping
blit in Phase 7.

### Swapchain Requirement

The swapchain must have `VK_IMAGE_USAGE_TRANSFER_DST_BIT` to be a blit
destination. Supported on all desktop GPUs. Verify at runtime via
`VkSurfaceCapabilitiesKHR::supportedUsageFlags`.

### Pipeline Colour Format

The mesh shader pipeline's `VkPipelineRenderingCreateInfo` must specify
`R16G16B16A16_SFLOAT` as the colour attachment format instead of the
swapchain's `B8G8R8A8_SRGB`.

---

## Tonemapping (Phase 7 Preview)

Phase 7 will replace the clamping blit with a compute pass that applies
tonemapping before writing to the swapchain. Two candidates:

**ACES Filmic** (industry standard, used by Unreal Engine):

```hlsl
float3 ACESFilm(float3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
```

Good contrast, slight hue shift toward white for very bright values.

**Reinhard** (simpler):

```hlsl
colour = colour / (colour + 1.0);
```

Less desaturation but no toe (blacks stay linear).

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

*Last updated: 2026-04-05*
