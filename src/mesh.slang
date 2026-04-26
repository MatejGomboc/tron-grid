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

// Mesh shader — outputs one meshlet per workgroup.
// Vertices are duplicated per triangle so each gets unique barycentric
// coordinates for wireframe edge detection in the fragment shader.

struct CameraData {
    float4x4 view;
    float4x4 projection;
    float4x4 inv_view_projection; // Inverse view-projection (for skybox ray reconstruction).
    float4x4 prev_view_projection; // Previous frame's view-projection (for motion vectors).
    float3 camera_pos; // Camera world-space position (for view vector in PBR).
    uint frame_count; // Frame counter (for pseudo-random sampling).
    uint emissive_count; // Number of emissive triangles.
    float total_emissive_power; // Sum of all emissive triangle powers.
    uint screen_width; // Render target width in pixels.
    uint screen_height; // Render target height in pixels.
};

// Must match C++ ObjectData struct layout (including padding).
struct ObjectData {
    float4x4 model;
    uint meshlet_offset;
    uint meshlet_count;
    uint material_index; // Index into the material SSBO.
    uint pad1;
};

// Must match C++ Material struct layout (48 bytes, 16-byte aligned).
struct MaterialData {
    float3 base_colour;
    float roughness;
    float3 emissive;
    float emissive_strength;
    float metallic;
    float ior;
    float opacity;
    float pad;
};

struct MeshletDesc {
    uint vertex_offset;
    uint vertex_count;
    uint triangle_offset;
    uint triangle_count;
};

// Layout must match C++ Vertex struct (48 bytes, 16-byte aligned).
struct Vertex {
    float pos_x, pos_y, pos_z;
    float norm_x, norm_y, norm_z;
    float uv_x, uv_y;
    float smooth_nx, smooth_ny, smooth_nz;
    float vertex_pad;
};

// Task payload — received from the task shader.
struct TaskPayload {
    uint object_index;
    uint meshlet_offset;
    uint meshlet_count;
};

struct MeshOutput {
    float4 position : SV_Position;
    float3 barycentric : TEXCOORD0; // Barycentric coordinates for wireframe edge detection.
    float3 world_position : TEXCOORD1; // World-space position for shadow ray origin.
    nointerpolation float3 world_normal : TEXCOORD2; // World-space face normal (flat shading).
    nointerpolation uint material_index : TEXCOORD3; // Index into the material SSBO.
    float4 prev_clip_pos : TEXCOORD4; // Previous frame's clip-space position (for motion vectors).
    float3 smooth_normal : TEXCOORD5; // Per-vertex smooth normal (interpolated, for reflections).
    nointerpolation uint instance_id : TEXCOORD6; // TLAS instance index — fragTransparent skips self-hits during refraction ray traversal.
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraData> camera;

[[vk::binding(1, 0)]]
StructuredBuffer<ObjectData> objects;

[[vk::binding(3, 0)]]
StructuredBuffer<MeshletDesc> meshlet_descs;

[[vk::binding(4, 0)]]
StructuredBuffer<Vertex> vertices;

[[vk::binding(5, 0)]]
StructuredBuffer<uint> meshlet_vertex_indices;

[[vk::binding(6, 0)]]
StructuredBuffer<uint8_t> meshlet_triangle_indices;

[[vk::binding(7, 0)]]
RaytracingAccelerationStructure scene_tlas;

[[vk::binding(8, 0)]]
StructuredBuffer<MaterialData> materials;

// Emissive triangle for area light sampling — must match C++ EmissiveTriangle struct.
struct EmissiveTriangle {
    float3 v0;
    float area;
    float3 v1;
    float cdf;
    float3 v2;
    float pad0;
    float3 emissive;
    float pad1;
};

[[vk::binding(9, 0)]]
StructuredBuffer<EmissiveTriangle> emissive_triangles;

// Per-pixel reservoir for ReSTIR temporal reuse — must match C++ Reservoir struct (80 bytes).
struct Reservoir {
    float3 y_pos; // Selected light sample position.
    float w_sum; // Sum of RIS weights.
    float3 y_emissive; // Emissive radiance at the selected sample.
    uint M; // Number of candidates merged; clamped proportionally (with w_sum) during temporal/spatial merges.
    float3 y_normal; // Light surface normal at the selected sample.
    float W; // Final contribution weight: w_sum / (M × p_hat(y)).
    float3 indirect; // Accumulated indirect radiance (single-bounce GI).
    float ao; // Accumulated ambient-occlusion visibility in [0, 1] (1 = fully lit, 0 = fully occluded).
    float3 shading_pos; // Shading-surface world position of the pixel that wrote this reservoir.
    uint shading_normal_oct; // Shading-surface normal, octahedral-packed to 32 bits.
};

[[vk::binding(10, 0)]]
RWStructuredBuffer<Reservoir> reservoir_current; // Current frame — write.

[[vk::binding(11, 0)]]
StructuredBuffer<Reservoir> reservoir_previous; // Previous frame — read.

// Canonical ReSTIR temporal M clamp: prev_r.M is capped at TEMPORAL_M_CAP × current M,
// with prev_r.w_sum scaled by the same ratio so that the invariant W × M × p_hat = w_sum
// is preserved (Bitterli et al. 2020). 20× is the value used in the original paper and
// canonical implementations (RTXDI). Applied at READ time during temporal merge.
static const uint TEMPORAL_M_CAP = 20;

// Per-neighbour spatial M cap — same proportional-scaling pattern as temporal.
static const uint SPATIAL_M_CAP = 100;

// Spatial reuse — merge reservoirs from neighbouring pixels for faster convergence.
static const uint NUM_SPATIAL_NEIGHBOURS = 5; // Number of random neighbours to merge.
static const float SPATIAL_RADIUS = 20.0; // Pixel radius for neighbour sampling.

// Geometric similarity thresholds for reprojection / neighbour rejection.
// Rejects samples whose underlying shading surface differs too much from the current pixel —
// prevents light-leaking across silhouettes and ghosting on disocclusion. Thresholds are
// permissive enough that typical inter-pixel world-space deltas on flat terrain pass; tight
// enough that a reservoir from a tube silhouetted in front of terrain is rejected.
// Per Bitterli et al. / RTXDI defaults: normal-dot ≥ 0.906 (~25° cone). Position delta
// uses 1 metre as a generous cap for a Grid-scale scene (camera ~5–15 m up, tile spacing
// ~1 m); tighter would reject legitimate inter-pixel parallax on distant terrain.
static const float RESTIR_MAX_POS_DELTA = 1.0; // Reject if |Δposition| exceeds this (metres).
static const float RESTIR_MIN_NORMAL_DOT = 0.906; // Reject if dot(n_current, n_neighbour) < this.

// Firefly clamp on per-sample indirect contributions. The second-bounce emissive
// sampling geometry term NdotL * cos_light / dist² can explode when a terrain hit
// happens to land within centimetres of a neon tube and the random CDF picks that
// tube as the bounce light. Without a clamp these fireflies persist in the EMA
// forever and propagate visually via bloom. Value picked empirically — well above
// plausible scene luminance (neon is 15, orb is 20) but below where a single bad
// sample can dominate the accumulator.
static const float INDIRECT_FIREFLY_MAX = 30.0;

// Minimum GI-bounce light distance (squared). The primary candidate loop already
// uses max(dist_sq, 1e-4); the GI bounce path takes a tighter floor because the
// bounce hit can land extremely close to an emissive tube, and distances under
// ~10 cm already produce fireflies faster than the EMA can smooth them.
static const float GI_MIN_LIGHT_DIST_SQ = 0.04;

// ── Ray-traced ambient occlusion (Etape 39) ──
// 1 cosine-weighted hemisphere ray per fragment per frame, short TMax so the
// AO captures local contact shadows (terrace corners, tube bases) without
// darkening the scene globally. Visibility averaged across frames via the
// reservoir's EMA and across neighbour pixels via the spatial reuse pass.
// Physical meaning: (1/π) ∫_Ω V(ω) cos(θ) dω — the Lambertian AO integral;
// cosine-weighted sampling is the unbiased estimator.
static const float AO_MAX_DIST = 2.0; // Occluder search radius in metres.

// ── Neon tube constants (per-fragment wireframe pattern, not per-object material) ──

// Dual-colour palette: cyan primary grid with orange accents on major grid lines.
static const float3 CYAN_EMISSIVE_COLOUR = float3(0.0, 0.8, 1.0); // Cyan neon — primary grid colour.
static const float3 ORANGE_EMISSIVE_COLOUR = float3(1.0, 0.03, 0.0); // Orange neon — accent on major grid lines.
static const float TUBE_EMISSIVE_STRENGTH = 15.0; // HDR intensity — well above 1.0.
static const float TUBE_ROUGHNESS = 1.0; // No specular — pure emissive glow.
static const float MAJOR_GRID_SPACING = 8.0; // Every Nth grid cell gets orange neon tubes.

// Wireframe edge detection — screen-space derivative-based antialiasing.
// WIRE_THICKNESS is the solid core width in pixels; WIRE_FALLOFF is the soft
// antialiased edge width in pixels. Together they produce consistent wire width
// regardless of triangle size or camera distance (Wunkolo 2022).
static const float WIRE_THICKNESS = 0.1;
static const float WIRE_FALLOFF = 0.2;
static const float REFLECTED_WIRE_WIDTH = 0.02; // Fixed barycentric-space threshold for RT reflections.

// PBR constants.
static const float PI = 3.14159265359;
static const float MIN_ROUGHNESS = 0.089; // Prevents NDF singularity and float16 denormals.
static const float3 F0_DIELECTRIC = float3(0.04, 0.04, 0.04); // Default dielectric F0 (IOR ~1.5).
static const float AMBIENT = 0.03; // Faint ambient — obsidian is nearly invisible without direct light.

// Ray tracing constants.
static const float SELF_INTERSECTION_OFFSET = 0.002; // Normal offset to prevent self-intersection.
static const float RAY_T_MIN = 0.001; // Minimum ray distance.

// ── PBR functions (Cook-Torrance BRDF) ──

// GGX / Trowbridge-Reitz normal distribution function.
// Filament's numerically stable form — avoids float16 catastrophic cancellation.
float D_GGX(float NdotH, float roughness)
{
    float a = NdotH * roughness;
    float k = roughness / (1.0 - NdotH * NdotH + a * a);
    return k * k * (1.0 / PI);
}

// Height-correlated Smith-GGX visibility function.
// Folds the 1/(4*NdotV*NdotL) denominator into the geometry term.
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
    float a2 = roughness * roughness;
    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    // Clamp denominator to prevent float16 overflow (inf) at extreme grazing angles.
    return 0.5 / max(GGXV + GGXL, 1e-4);
}

// Schlick Fresnel approximation.
float3 F_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Selects the neon tube emissive colour based on world position.
// Cells along every Nth row or column (major grid lines) glow orange;
// all other cells glow cyan. Creates a coarse orange supergrid overlay.
// Uses floor(abs(x)) (not abs(floor(x))) so classification is symmetric around
// the origin — without this, negative x shifts the orange bands by one cell.
float3 neonEmissiveColour(float3 world_pos)
{
    float mod_x = fmod(floor(abs(world_pos.x)), MAJOR_GRID_SPACING);
    float mod_z = fmod(floor(abs(world_pos.z)), MAJOR_GRID_SPACING);
    bool is_orange = (mod_x < 0.5) || (mod_z < 0.5);
    return is_orange ? ORANGE_EMISSIVE_COLOUR : CYAN_EMISSIVE_COLOUR;
}

// Octahedral normal encoding — packs a unit vector into a single uint32 with
// ~1° worst-case error. Used to store the shading-surface normal in the
// 80-byte reservoir without growing the struct to 96 bytes. See Cigolle et al.
// "A Survey of Efficient Representations for Independent Unit Vectors" (2014).
uint packOctahedralNormal(float3 n)
{
    n = normalize(n);
    float abs_sum = abs(n.x) + abs(n.y) + abs(n.z);
    float2 p = n.xy / max(abs_sum, 1e-6);
    if (n.z < 0.0) {
        float2 snap = float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
        p = (float2(1.0, 1.0) - abs(p.yx)) * snap;
    }
    // Quantise to 16 bits per axis, centred on [0, 1].
    uint2 q = uint2(clamp(p * 0.5 + 0.5, 0.0, 1.0) * 65535.0 + 0.5);
    return q.x | (q.y << 16);
}

float3 unpackOctahedralNormal(uint packed)
{
    float2 q = float2(packed & 0xFFFFu, (packed >> 16) & 0xFFFFu) / 65535.0;
    float2 p = q * 2.0 - 1.0;
    float3 n = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) {
        float2 snap = float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (float2(1.0, 1.0) - abs(p.yx)) * snap;
    }
    return normalize(n);
}

// Geometric similarity test used to reject reprojected / neighbour reservoirs
// whose underlying shading surface differs from the current pixel. Prevents
// light bleeding across silhouette edges and ghosting on disocclusion.
bool reservoirSurfaceMatches(Reservoir r, float3 current_pos, float3 current_normal)
{
    if (r.M == 0) {
        return false;
    }
    float3 delta = r.shading_pos - current_pos;
    if (dot(delta, delta) > RESTIR_MAX_POS_DELTA * RESTIR_MAX_POS_DELTA) {
        return false;
    }
    float3 stored_normal = unpackOctahedralNormal(r.shading_normal_oct);
    if (dot(stored_normal, current_normal) < RESTIR_MIN_NORMAL_DOT) {
        return false;
    }
    return true;
}

// ── Pseudo-random hash (PCG) ──

// PCG hash — fast, high-quality 32-bit hash.
uint pcgHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Converts a uint hash to a float in [0, 1).
float hashToFloat(uint hash)
{
    return float(hash) / 4294967296.0;
}

// Luminance of an HDR colour (BT.709 coefficients).
float luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Builds an orthonormal basis from a normal (Frisvad's method for robust tangent frame).
void buildOrthonormalBasis(float3 n, out float3 t, out float3 b)
{
    if (n.z < -0.9999) {
        t = float3(0.0, -1.0, 0.0);
        b = float3(-1.0, 0.0, 0.0);
        return;
    }
    float a = 1.0 / (1.0 + n.z);
    float d = -n.x * n.y * a;
    t = float3(1.0 - n.x * n.x * a, d, -n.x);
    b = float3(d, 1.0 - n.y * n.y * a, -n.y);
}

// Binary search the CDF to select an emissive triangle index.
// Defensive: callers already guard on camera.emissive_count > 0, but without this
// early-out count=0 would produce hi = UINT_MAX and loop ~32 times reading OOB.
uint searchCDF(float r, uint count)
{
    if (count == 0) {
        return 0;
    }
    uint lo = 0;
    uint hi = count - 1;
    while (lo < hi) {
        uint mid = (lo + hi) / 2;
        if (emissive_triangles[mid].cdf < r) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

// Fragment shader — obsidian floor with neon tube edges, Cook-Torrance PBR,
// emissive area light sampling, RT shadows and reflections.
[shader("fragment")]
float4 fragMain(MeshOutput input, uint sample_index : SV_SampleIndex) : SV_Target
{
    // Force input.instance_id into this fragment shader's SPIR-V input interface so the
    // validation layer does not flag a mesh-stage Output (Location 6) with no matching
    // fragment Input. Only fragTransparent logically needs the field, but the mesh shader
    // emits it for both fragment entry points; without this reference Slang strips it from
    // fragMain's interface and Vulkan reports a (harmless) "Output declared but no Input"
    // warning at pipeline creation. The discard branch never triggers — scene instance
    // IDs are always far below 0xFFFFFFFF.
    if (input.instance_id == 0xFFFFFFFFu) {
        discard;
    }

    // With MSAA + full sample-rate shading the fragment shader runs once per
    // covered sample. All samples of a given pixel share the same pixel_idx
    // and would non-atomically race to write the same reservoir slot. Only
    // sample 0 performs the reservoir write — samples 1..N still compute
    // their own colour for per-sample coverage but skip the SSBO store.
    bool write_reservoir = (sample_index == 0);

    // Load material from the SSBO.
    MaterialData mat = materials[input.material_index];

    uint pixel_x = uint(input.position.x);
    uint pixel_y = uint(input.position.y);
    bool pixel_in_bounds = (pixel_x < camera.screen_width) && (pixel_y < camera.screen_height);
    uint pixel_idx = pixel_y * camera.screen_width + pixel_x;

    // Pure emissive objects (orb, neon tubes) — no wireframe or PBR, just emit light.
    // Zero the reservoir slot so spatial reuse neighbours don't read stale data.
    if (mat.emissive_strength > 0.0) {
        if (write_reservoir && pixel_in_bounds) {
            Reservoir empty;
            empty.y_pos = float3(0.0, 0.0, 0.0);
            empty.w_sum = 0.0;
            empty.y_emissive = float3(0.0, 0.0, 0.0);
            empty.M = 0;
            empty.y_normal = float3(0.0, 0.0, 0.0);
            empty.W = 0.0;
            empty.indirect = float3(0.0, 0.0, 0.0);
            empty.ao = 1.0; // Emissive pixels are self-lit; AO is meaningless here — store full visibility so nothing neighbouring can read a dark value.
            empty.shading_pos = input.world_position;
            empty.shading_normal_oct = 0u;
            reservoir_current[pixel_idx] = empty;
        }
        return float4(mat.emissive * mat.emissive_strength, 1.0);
    }

    // Antialiased edge detection — screen-space derivatives give consistent pixel-width
    // wireframe regardless of triangle size. smoothstep produces a soft falloff at edges.
    float3 d_bary = fwidth(input.barycentric);
    float3 remap = smoothstep(d_bary * WIRE_THICKNESS, d_bary * (WIRE_THICKNESS + WIRE_FALLOFF), input.barycentric);
    float wire = min(remap.x, min(remap.y, remap.z));

    // Determine neon tube colour — cyan or orange based on grid position.
    float3 tube_colour = neonEmissiveColour(input.world_position);

    // Blend material properties between neon tube and surface material from SSBO.
    float3 base_colour = lerp(float3(0.0, 0.0, 0.0), mat.base_colour, wire);
    float perceptual_roughness = lerp(TUBE_ROUGHNESS, mat.roughness, wire);
    float roughness = max(perceptual_roughness * perceptual_roughness, MIN_ROUGHNESS);
    float3 emissive = lerp(tube_colour * TUBE_EMISSIVE_STRENGTH, float3(0.0, 0.0, 0.0), wire);

    // Vectors.
    float3 N_geo = normalize(input.world_normal); // Flat geometric normal (per-face).
    float3 N_smooth = normalize(input.smooth_normal); // Smooth heightmap-gradient normal (for reflections).
    float3 V = normalize(camera.camera_pos - input.world_position);

    // Two-sided lighting — if the geometric face points away from the camera
    // (typical for the far side of a steep terrace), flip BOTH the shading
    // normal and the smooth-normal so reflections, BRDF, and self-intersection
    // offsets stay consistent. Previously only N (shading) was flipped, leaving
    // N_smooth pointing into the surface and producing degenerate reflections.
    bool front_facing = dot(N_geo, V) >= 0.0;
    float3 N = front_facing ? N_geo : -N_geo;
    float3 N_smooth_front = front_facing ? N_smooth : -N_smooth;
    float NdotV = abs(dot(N, V)) + 1e-5;

    // Pre-compute the PBR Fresnel F0 from the material. Used by both direct
    // lighting (via VdotH) and the reflection path (via NdotV). Metallic
    // materials tint F0 with base_colour; dielectrics use the Snell-derived
    // scalar F0 = ((n-1)/(n+1))^2.
    float ior_ratio = (mat.ior - 1.0) / (mat.ior + 1.0);
    float3 F0_dielectric = float3(ior_ratio * ior_ratio, ior_ratio * ior_ratio, ior_ratio * ior_ratio);
    float3 F0_mat = lerp(F0_dielectric, mat.base_colour, mat.metallic);
    float3 F_view = F_Schlick(max(dot(N, V), 0.0), F0_mat);

    // Pack the shading-surface normal once — used when storing the reservoir.
    uint shading_normal_packed = packOctahedralNormal(N);

    // ── ReSTIR DI temporal reuse (emissive area light sampling) ──

    // Direct lighting is split into diffuse and specular lobes so that ambient
    // occlusion can multiply the diffuse component only (canonical: AO darkens
    // Lambertian interreflection, not specular). Reflection and direct spec
    // survive AO untouched, matching the hybrid-rendering composite.
    float3 direct_diffuse = float3(0.0, 0.0, 0.0);
    float3 direct_specular = float3(0.0, 0.0, 0.0);
    float3 indirect_gi = base_colour * AMBIENT; // Fallback flat ambient if no emissive triangles.
    float ao_factor = 1.0; // AO visibility in [0, 1]; defaults to fully lit when ReSTIR path is skipped.

    if (camera.emissive_count > 0)
    {
        // ── Generate candidate sample (same as 37a) ──

        uint pixel_seed = pixel_x * 1973u + pixel_y * 9277u + camera.frame_count * 26699u;
        uint hash0 = pcgHash(pixel_seed);
        uint hash1 = pcgHash(hash0);
        uint hash2 = pcgHash(hash1);

        float r0 = hashToFloat(hash0);
        float r1 = hashToFloat(hash1);
        float r2 = hashToFloat(hash2);

        uint tri_idx = searchCDF(r0, camera.emissive_count);
        EmissiveTriangle etri = emissive_triangles[tri_idx];

        float su = sqrt(r1);
        float bary_u = 1.0 - su;
        float bary_v = su * r2;
        float3 light_point = bary_u * etri.v0 + bary_v * etri.v1 + (1.0 - bary_u - bary_v) * etri.v2;
        float3 light_normal = normalize(cross(etri.v1 - etri.v0, etri.v2 - etri.v0));

        // Evaluate target function p_hat for the candidate at the current shading point.
        float3 to_light = light_point - input.world_position;
        float dist_sq = dot(to_light, to_light);
        float dist = max(sqrt(dist_sq), 1e-4);
        float3 L = to_light / dist;
        float NdotL_cand = max(dot(N, L), 0.0);
        float cos_light = abs(dot(light_normal, -L));
        float geometry = NdotL_cand * cos_light / max(dist_sq, 1e-4);
        float p_hat_cand = luminance(etri.emissive) * geometry;

        // Source PDF from the power-weighted CDF.
        float pdf_source = max(luminance(etri.emissive) / camera.total_emissive_power, 1e-8);

        // RIS weight for the candidate.
        float w_cand = (pdf_source > 0.0) ? (p_hat_cand / pdf_source) : 0.0;

        // Initialise reservoir with the candidate.
        Reservoir r;
        r.y_pos = light_point;
        r.y_emissive = etri.emissive;
        r.y_normal = light_normal;
        r.w_sum = w_cand;
        r.M = 1;
        r.W = 0.0;
        r.indirect = float3(0.0, 0.0, 0.0);
        r.ao = 1.0; // Fully lit until proven otherwise by the AO trace below.
        r.shading_pos = input.world_position;
        r.shading_normal_oct = shading_normal_packed;

        // ── Temporal reuse: merge with previous frame's reservoir ──

        // Reproject current world position to previous frame's screen space.
        // prev_clip_pos uses the raw projection matrix (math convention: Y+ = up),
        // but screen pixels use Vulkan convention (Y=0 at top). Negate NDC Y to match.
        float4 prev_clip = input.prev_clip_pos;
        float2 prev_ndc = prev_clip.xy / max(prev_clip.w, 1e-6);
        float2 prev_uv = prev_ndc * float2(0.5, -0.5) + 0.5;
        int2 prev_pixel = int2(prev_uv * float2(camera.screen_width, camera.screen_height));

        bool prev_in_screen = (prev_pixel.x >= 0) && (prev_pixel.x < int(camera.screen_width))
            && (prev_pixel.y >= 0) && (prev_pixel.y < int(camera.screen_height))
            && (prev_clip.w > 0.0);

        if (prev_in_screen)
        {
            uint prev_idx = uint(prev_pixel.y) * camera.screen_width + uint(prev_pixel.x);
            Reservoir prev_r = reservoir_previous[prev_idx];

            // Reject reprojected reservoirs whose shading surface differs — prevents ghosting
            // on disocclusion and when a moving silhouette pulls a tube in front of terrain.
            if (reservoirSurfaceMatches(prev_r, input.world_position, N))
            {
                // Canonical ReSTIR temporal M clamp (Bitterli et al. 2020). Caps the
                // historical reservoir at TEMPORAL_M_CAP × current M, scaling w_sum by
                // the same ratio so that the invariant W × M × p_hat = w_sum is preserved.
                // Prevents stale history from dominating fresh candidates.
                if (prev_r.M > TEMPORAL_M_CAP * r.M) {
                    float scale = float(TEMPORAL_M_CAP * r.M) / float(prev_r.M);
                    prev_r.w_sum *= scale;
                    prev_r.M = TEMPORAL_M_CAP * r.M;
                }

                // Evaluate p_hat at the current shading point for the temporal sample.
                float3 to_temporal = prev_r.y_pos - input.world_position;
                float temp_dist_sq = dot(to_temporal, to_temporal);
                float temp_dist = max(sqrt(temp_dist_sq), 1e-4);
                float3 L_temp = to_temporal / temp_dist;
                float NdotL_temp = max(dot(N, L_temp), 0.0);
                float cos_light_temp = abs(dot(prev_r.y_normal, -L_temp));
                float geometry_temp = NdotL_temp * cos_light_temp / max(temp_dist_sq, 1e-4);
                float p_hat_temp = luminance(prev_r.y_emissive) * geometry_temp;

                // Standard RIS temporal weight: w_temporal = p_hat_new × W_prev × M_prev.
                float w_temporal = p_hat_temp * prev_r.W * float(prev_r.M);

                // Merge: add temporal weight and probabilistically select.
                r.w_sum += w_temporal;
                r.M += prev_r.M;

                uint hash3 = pcgHash(hash2 + 54321u);
                float select_r = hashToFloat(hash3);
                if ((r.w_sum > 0.0) && (select_r < (w_temporal / r.w_sum)))
                {
                    r.y_pos = prev_r.y_pos;
                    r.y_emissive = prev_r.y_emissive;
                    r.y_normal = prev_r.y_normal;
                }

                // Carry forward the previous frame's accumulated indirect GI and AO.
                r.indirect = prev_r.indirect;
                r.ao = prev_r.ao;
            }
        }

        // Save M before spatial merge — used for indirect GI EMA alpha.
        // Spatial M inflates the count but doesn't contribute new indirect samples.
        uint temporal_M = r.M;

        // ── Spatial reuse: merge with neighbouring pixels' reservoirs ──

        // Spatial AO accumulator — averages AO across geometrically-similar
        // neighbours after the spatial ReSTIR merge is done. Each accepted
        // neighbour contributes one AO sample from its own reservoir; with
        // 1 AO ray per pixel per frame and ~20 temporal frames × up to
        // NUM_SPATIAL_NEIGHBOURS neighbours, effective AO sample count
        // approaches ~100, denoising the single-ray RTAO without a separate
        // filter pass.
        float ao_spatial_sum = 0.0;
        uint ao_spatial_count = 0u;

        // Spatial accumulator for the single-bounce indirect GI term (Phase 8 Etape 42b).
        // Indirect already gets temporal smoothing via EMA into r.indirect, but each
        // pixel's hemisphere sample is independent — adjacent pixels can land on very
        // different lighting contexts and the temporal-only signal flickers. Averaging
        // accepted neighbours' indirect (gated by the same geometric reject as AO and the
        // ReSTIR merge) reduces inter-pixel variance at zero new state cost.
        float3 indirect_spatial_sum = float3(0.0, 0.0, 0.0);
        uint indirect_spatial_count = 0u;

        uint spatial_hash = pcgHash(pixel_seed + 99991u);
        for (uint s = 0; s < NUM_SPATIAL_NEIGHBOURS; ++s)
        {
            // Random neighbour offset within SPATIAL_RADIUS disc.
            uint hash_x = pcgHash(spatial_hash + s * 2u);
            uint hash_y = pcgHash(spatial_hash + s * 2u + 1u);
            float angle = hashToFloat(hash_x) * 2.0 * PI;
            float radius = sqrt(hashToFloat(hash_y)) * SPATIAL_RADIUS;
            int2 neighbour_pixel = int2(pixel_x, pixel_y) + int2(int(cos(angle) * radius), int(sin(angle) * radius));

            // Bounds check — also skip the current pixel (zero-radius sample self-matches).
            if ((neighbour_pixel.x < 0) || (neighbour_pixel.x >= int(camera.screen_width))
                || (neighbour_pixel.y < 0) || (neighbour_pixel.y >= int(camera.screen_height))
                || ((neighbour_pixel.x == int(pixel_x)) && (neighbour_pixel.y == int(pixel_y))))
            {
                continue;
            }

            uint neighbour_idx = uint(neighbour_pixel.y) * camera.screen_width + uint(neighbour_pixel.x);
            Reservoir neighbour_r = reservoir_previous[neighbour_idx];

            // Reject empty, sky, or geometrically-dissimilar neighbours — prevents light
            // leaking across silhouettes where the neighbour pixel was a different surface.
            if (!reservoirSurfaceMatches(neighbour_r, input.world_position, N)) {
                continue;
            }

            // Accumulate the neighbour's AO value — same surface-similarity test that
            // gates the ReSTIR merge ensures we don't blur AO across silhouette edges.
            ao_spatial_sum += neighbour_r.ao;
            ++ao_spatial_count;

            // Accumulate the neighbour's indirect GI for spatial smoothing (Etape 42b).
            // Same gating — the geometric reject above prevents leaking radiance across
            // silhouettes and discontinuities.
            indirect_spatial_sum += neighbour_r.indirect;
            ++indirect_spatial_count;

            // Same canonical proportional clamp as temporal, but with a more generous cap
            // because spatial neighbours bring in fresh spatial information that we want to
            // weight reasonably. Scaling w_sum keeps the W × M × p_hat invariant intact.
            if (neighbour_r.M > SPATIAL_M_CAP) {
                float scale = float(SPATIAL_M_CAP) / float(neighbour_r.M);
                neighbour_r.w_sum *= scale;
                neighbour_r.M = SPATIAL_M_CAP;
            }

            // Evaluate p_hat at the current shading point for the spatial sample.
            float3 to_spatial = neighbour_r.y_pos - input.world_position;
            float sp_dist_sq = dot(to_spatial, to_spatial);
            float sp_dist = max(sqrt(sp_dist_sq), 1e-4);
            float3 L_sp = to_spatial / sp_dist;
            float NdotL_sp = max(dot(N, L_sp), 0.0);
            float cos_light_sp = abs(dot(neighbour_r.y_normal, -L_sp));
            float geometry_sp = NdotL_sp * cos_light_sp / max(sp_dist_sq, 1e-4);
            float p_hat_sp = luminance(neighbour_r.y_emissive) * geometry_sp;

            // RIS weight for the spatial neighbour's contribution.
            float w_spatial = p_hat_sp * neighbour_r.W * float(neighbour_r.M);

            // Merge into reservoir.
            r.w_sum += w_spatial;
            r.M += neighbour_r.M;

            uint hash_sel = pcgHash(spatial_hash + s * 2u + 7777u);
            float sel = hashToFloat(hash_sel);
            if ((r.w_sum > 0.0) && (sel < (w_spatial / r.w_sum)))
            {
                r.y_pos = neighbour_r.y_pos;
                r.y_emissive = neighbour_r.y_emissive;
                r.y_normal = neighbour_r.y_normal;
            }
        }

        // Blend accumulated neighbour AO with the current pixel's (temporal) AO.
        // Weighted average where the current pixel counts as one sample alongside
        // each accepted neighbour — cheaper and steadier than re-tracing AO rays
        // at neighbour positions and biased toward the current pixel's own history.
        if (ao_spatial_count > 0u) {
            r.ao = (r.ao + ao_spatial_sum) / (1.0 + float(ao_spatial_count));
        }

        // Same blend for the indirect GI term (Etape 42b). Order matters: spatial
        // smoothing of the temporal history happens FIRST, then the new hemisphere
        // bounce sample is EMA-blended into r.indirect at the end of this block.
        // Effect: neighbours' temporally-smoothed indirect contribute to ours, the
        // new bounce updates the now-smoothed value, and we write back. Reduces
        // per-pixel hemisphere-sampling variance without re-tracing GI rays.
        if (indirect_spatial_count > 0u) {
            r.indirect = (r.indirect + indirect_spatial_sum) / (1.0 + float(indirect_spatial_count));
        }

        // Compute final contribution weight W for the selected sample. The canonical
        // ReSTIR formula (Bitterli et al. 2020): W = (1/p_hat) × (w_sum / M), with M
        // being the combined count from candidate + temporal + spatial merges, and both
        // temporal and spatial contributions already having had w_sum scaled to match
        // any M clamp applied during those merges — so the invariant W × M × p_hat = w_sum
        // holds for the stored reservoir too.
        float3 to_selected = r.y_pos - input.world_position;
        float sel_dist_sq = dot(to_selected, to_selected);
        float sel_dist = max(sqrt(sel_dist_sq), 1e-4);
        float3 L_sel = to_selected / sel_dist;
        float NdotL_sel = max(dot(N, L_sel), 0.0);
        float cos_light_sel = abs(dot(r.y_normal, -L_sel));
        float geometry_sel = NdotL_sel * cos_light_sel / max(sel_dist_sq, 1e-4);
        float p_hat_sel = luminance(r.y_emissive) * geometry_sel;

        r.W = ((p_hat_sel > 0.0) && (r.M > 0)) ? (r.w_sum / (float(r.M) * p_hat_sel)) : 0.0;

        // ── Ray-traced ambient occlusion (one cosine-weighted hemisphere ray) ──
        // The AO integral (1/π) ∫_Ω V(ω) cos(θ) dω is the Lambertian visibility integral;
        // cosine-weighted sampling is the unbiased Monte Carlo estimator. One ray per
        // fragment per frame, averaged over time (EMA) and across geometrically-similar
        // neighbours (spatial sum above) converges to noise-free AO without a separate
        // filter pass. TMax is short (AO_MAX_DIST) so only local contact occlusion —
        // terrace corners, tube bases — contributes.
        uint hash_ao0 = pcgHash(pixel_seed + 13337u);
        uint hash_ao1 = pcgHash(hash_ao0);
        float ao_r1 = hashToFloat(hash_ao0);
        float ao_r2 = hashToFloat(hash_ao1);

        float ao_cos_theta = sqrt(ao_r2);
        float ao_sin_theta = sqrt(1.0 - ao_r2);
        float ao_phi = 2.0 * PI * ao_r1;

        float3 ao_tangent;
        float3 ao_bitangent;
        buildOrthonormalBasis(N, ao_tangent, ao_bitangent);
        float3 ao_dir = ao_tangent * (ao_sin_theta * cos(ao_phi)) + N * ao_cos_theta + ao_bitangent * (ao_sin_theta * sin(ao_phi));

        RayDesc ao_ray;
        ao_ray.Origin = input.world_position + N * SELF_INTERSECTION_OFFSET;
        ao_ray.Direction = ao_dir;
        ao_ray.TMin = RAY_T_MIN;
        ao_ray.TMax = AO_MAX_DIST;

        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> ao_query;
        ao_query.TraceRayInline(scene_tlas, RAY_FLAG_NONE, 0xFF, ao_ray);
        ao_query.Proceed();

        float ao_sample = (ao_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;

        // Temporal EMA — same alpha derivation as the indirect-GI term (pre-spatial M
        // so repeated merges don't dilute the update rate below a single fresh sample).
        float ao_alpha = (temporal_M > 1) ? (1.0 / float(temporal_M)) : 1.0;
        r.ao = lerp(r.ao, ao_sample, ao_alpha);
        ao_factor = r.ao;

        // ── Single-bounce indirect GI (cosine-weighted hemisphere sampling) ──

        // Russian roulette — terminate early for dark surfaces (most obsidian pixels).
        // The unbiased RR estimator is:
        //   E[V] = rr_prob × (sample / rr_prob) + (1 − rr_prob) × 0 = sample.
        // The zero contribution on rejection is essential — feeding it into the EMA
        // averages accepted samples (scaled up by 1/rr_prob) with the balancing
        // zeros from rejections. Skipping the update on rejection would collapse
        // the estimator to E[V | accepted] = sample / rr_prob, which is 1/rr_prob
        // too high (≈20× for dark surfaces) and produces a slow indirect saturation
        // feedback loop over many frames.
        float rr_prob = clamp(luminance(base_colour) * 10.0, 0.0, 0.95);
        uint hash_rr = pcgHash(pixel_seed + 77777u);
        float3 new_indirect = float3(0.0, 0.0, 0.0);

        if (hashToFloat(hash_rr) < rr_prob)
        {
            // Cosine-weighted hemisphere direction from the shading normal (Malley's method).
            uint hash_gi0 = pcgHash(hash_rr + 11111u);
            uint hash_gi1 = pcgHash(hash_gi0);
            float gi_r1 = hashToFloat(hash_gi0);
            float gi_r2 = hashToFloat(hash_gi1);

            float cos_theta = sqrt(gi_r2);
            float sin_theta = sqrt(1.0 - gi_r2);
            float phi = 2.0 * PI * gi_r1;

            float3 tangent;
            float3 bitangent;
            buildOrthonormalBasis(N, tangent, bitangent);
            float3 gi_dir = tangent * (sin_theta * cos(phi)) + N * cos_theta + bitangent * (sin_theta * sin(phi));

            // Trace indirect ray.
            float3 gi_origin = input.world_position + N * SELF_INTERSECTION_OFFSET;

            RayDesc gi_ray;
            gi_ray.Origin = gi_origin;
            gi_ray.Direction = gi_dir;
            gi_ray.TMin = RAY_T_MIN;
            gi_ray.TMax = 200.0;

            // RAY_FLAG_NONE — consistent with the reflection path. Neon tube quads are
            // thin emitters; making GI rays cull back-faces would silently drop legitimate
            // emissive contribution from tubes hit on the "far" side of a thin quad.
            RayQuery<RAY_FLAG_NONE> gi_query;
            gi_query.TraceRayInline(scene_tlas, RAY_FLAG_NONE, 0xFF, gi_ray);
            gi_query.Proceed();

            if (gi_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            {
                // Evaluate incoming radiance at the bounce point.
                uint gi_hit_instance = gi_query.CommittedInstanceID();
                uint gi_hit_mat_idx = objects[gi_hit_instance].material_index;
                MaterialData gi_hit_mat = materials[gi_hit_mat_idx];

                float3 incoming = float3(0.0, 0.0, 0.0);
                if (gi_hit_mat.emissive_strength > 0.0) {
                    // Hit emissive geometry directly — use its radiance.
                    incoming = gi_hit_mat.emissive * gi_hit_mat.emissive_strength;
                } else {
                    // Hit non-emissive surface (terrain) — evaluate direct illumination.
                    //
                    // INVARIANT: non-emissive hits are always terrain (the only non-emissive
                    // material in the scene). Terrain is entity 0, and terrain vertices
                    // occupy the beginning of the combined vertex SSBO (offset 0). Terrain's
                    // BLAS uses a sequential index buffer (indices[i] = i), so the primitive
                    // index maps directly to the global vertex SSBO: triangle t reads
                    // vertices at indices {t*3, t*3+1, t*3+2}. WARNING — if a second
                    // non-emissive entity is ever added (or if terrain's vertex offset ever
                    // shifts away from 0), this lookup silently reads the wrong normal.
                    // A robust replacement would read a per-instance vertex base offset
                    // from the ObjectData SSBO.
                    float3 gi_hit_pos = gi_origin + gi_dir * gi_query.CommittedRayT();
                    uint gi_prim = gi_query.CommittedPrimitiveIndex();
                    Vertex gi_hit_vtx = vertices[gi_prim * 3];
                    float3 gi_hit_normal = normalize(float3(gi_hit_vtx.norm_x, gi_hit_vtx.norm_y, gi_hit_vtx.norm_z));

                    // Flip hit normal to face the incoming GI ray (two-sided).
                    // Now live code: GI rays no longer cull back-faces (see gi_query flags).
                    if (dot(gi_hit_normal, -gi_dir) < 0.0) {
                        gi_hit_normal = -gi_hit_normal;
                    }

                    uint hash_gi2 = pcgHash(hash_gi1 + 33333u);
                    uint hash_gi3 = pcgHash(hash_gi2);
                    uint hash_gi4 = pcgHash(hash_gi3);

                    uint gi_tri_idx = searchCDF(hashToFloat(hash_gi2), camera.emissive_count);
                    EmissiveTriangle gi_etri = emissive_triangles[gi_tri_idx];

                    float gi_su = sqrt(hashToFloat(hash_gi3));
                    float gi_bu = 1.0 - gi_su;
                    float gi_bv = gi_su * hashToFloat(hash_gi4);
                    float3 gi_light_pt = gi_bu * gi_etri.v0 + gi_bv * gi_etri.v1 + (1.0 - gi_bu - gi_bv) * gi_etri.v2;

                    float3 gi_to_light = gi_light_pt - gi_hit_pos;
                    float gi_light_dist_sq_raw = dot(gi_to_light, gi_to_light);
                    // Tighter floor than the primary loop — see GI_MIN_LIGHT_DIST_SQ.
                    float gi_light_dist_sq = max(gi_light_dist_sq_raw, GI_MIN_LIGHT_DIST_SQ);
                    float gi_light_dist = sqrt(gi_light_dist_sq);
                    float3 gi_L = gi_to_light / gi_light_dist;

                    // Full geometry term: NdotL at hit surface × cos_light at emissive surface / distance².
                    float gi_NdotL_hit = max(dot(gi_hit_normal, gi_L), 0.0);
                    float3 gi_light_n = normalize(cross(gi_etri.v1 - gi_etri.v0, gi_etri.v2 - gi_etri.v0));
                    float gi_cos_light = abs(dot(gi_light_n, -gi_L));
                    float gi_geometry = gi_NdotL_hit * gi_cos_light / gi_light_dist_sq;
                    float gi_pdf = max(luminance(gi_etri.emissive) / camera.total_emissive_power, 1e-8);

                    // Shadow ray from bounce hit point to the sampled light.
                    if (gi_NdotL_hit > 0.0) {
                        float3 gi_shadow_origin = gi_hit_pos + gi_hit_normal * SELF_INTERSECTION_OFFSET;
                        float gi_shadow_tmax = max(gi_light_dist - SELF_INTERSECTION_OFFSET, RAY_T_MIN + 0.001);

                        RayDesc gi_shadow_ray;
                        gi_shadow_ray.Origin = gi_shadow_origin;
                        gi_shadow_ray.Direction = gi_L;
                        gi_shadow_ray.TMin = RAY_T_MIN;
                        gi_shadow_ray.TMax = gi_shadow_tmax;

                        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> gi_shadow_query;
                        gi_shadow_query.TraceRayInline(scene_tlas, RAY_FLAG_NONE, 0xFF, gi_shadow_ray);
                        gi_shadow_query.Proceed();

                        bool gi_in_shadow = (gi_shadow_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT);
                        if (!gi_in_shadow) {
                            incoming = gi_etri.emissive * gi_geometry / gi_pdf * gi_hit_mat.base_colour / PI;
                        }
                    }
                }

                // Cosine-weighted estimator: base_colour * incoming / rr_prob.
                // (cos_theta and PI cancel between BRDF and hemisphere PDF.)
                new_indirect = base_colour * incoming / rr_prob;

                // Firefly clamp. The bounce geometry term + per-triangle PDF division
                // can still spike individual samples beyond the distance floor — clamp
                // them before feeding the EMA so a single bad sample can't pin the
                // accumulator at a huge value for the rest of the session.
                new_indirect = min(new_indirect, float3(INDIRECT_FIREFLY_MAX, INDIRECT_FIREFLY_MAX, INDIRECT_FIREFLY_MAX));
            }
        }

        // EMA update runs every frame — new_indirect is 0 when Russian roulette
        // rejected or the ray missed, which is the unbiased zero contribution the
        // RR estimator requires. Skipping the update would make the accumulator
        // converge to sample/rr_prob (1/rr_prob too high) instead of sample.
        float indirect_alpha = (temporal_M > 1) ? (1.0 / float(temporal_M)) : 1.0;
        r.indirect = lerp(r.indirect, new_indirect, indirect_alpha);
        indirect_gi = r.indirect;

        // Store reservoir for next frame's temporal reuse — sample 0 only.
        if (write_reservoir && pixel_in_bounds) {
            reservoir_current[pixel_idx] = r;
        }

        // ── Final shading: shadow ray + BRDF for the selected sample ──

        if ((NdotL_sel > 0.0) && (r.W > 0.0))
        {
            float3 origin = input.world_position + N * SELF_INTERSECTION_OFFSET;
            float ray_tmax = max(sel_dist - SELF_INTERSECTION_OFFSET, RAY_T_MIN + 0.001);

            RayDesc ray;
            ray.Origin = origin;
            ray.Direction = L_sel;
            ray.TMin = RAY_T_MIN;
            ray.TMax = ray_tmax;

            RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
            query.TraceRayInline(scene_tlas, RAY_FLAG_NONE, 0xFF, ray);
            query.Proceed();

            bool in_shadow = (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT);
            if (!in_shadow)
            {
                // Cook-Torrance BRDF for the selected light direction — unified F0
                // (metallic-aware, shared with the reflection path's F_view).
                float3 H = normalize(V + L_sel);
                float NdotH = clamp(dot(N, H), 0.0, 1.0);
                float VdotH = clamp(dot(V, H), 0.0, 1.0);

                float D_val = D_GGX(NdotH, roughness);
                float Vis = V_SmithGGXCorrelated(NdotV, NdotL_sel, roughness);
                float3 F = F_Schlick(VdotH, F0_mat);

                float3 spec_brdf = D_val * Vis * F;
                float3 diff_brdf = (1.0 - F) * (1.0 - mat.metallic) * base_colour / PI;

                // ReSTIR estimator split into diffuse and specular lobes — AO multiplies
                // the diffuse (Lambertian) contribution only; specular and RT reflection
                // stay at full brightness.
                float3 Lo_common = r.y_emissive * geometry_sel * r.W;
                direct_diffuse = diff_brdf * Lo_common;
                direct_specular = spec_brdf * Lo_common;
            }
        }
    }

    // ── RT reflection for obsidian surface ──

    float3 reflected_colour = float3(0.0, 0.0, 0.0);

    // Only trace reflections for obsidian-dominant fragments (wire > 0.5) that
    // front-face the camera (guard eliminates self-intersecting "into the surface"
    // rays at back-facing terrace sidewalls). Use the smooth-normal for the
    // reflection direction (continuous across terraces) but the geometric normal
    // for the origin offset (avoids placing the ray inside the sidewall when
    // N_smooth and N_geo disagree by ~90°).
    if ((wire > 0.5) && (F_view.x > 0.01) && front_facing)
    {
        float3 reflect_dir = reflect(-V, N_smooth_front);
        float3 reflect_origin = input.world_position + N * SELF_INTERSECTION_OFFSET;

        RayDesc reflect_ray;
        reflect_ray.Origin = reflect_origin;
        reflect_ray.Direction = reflect_dir;
        reflect_ray.TMin = RAY_T_MIN;
        reflect_ray.TMax = 200.0;

        // No back-face culling — neon tube quads must be visible from both sides in reflections.
        RayQuery<RAY_FLAG_NONE> reflect_query;
        reflect_query.TraceRayInline(scene_tlas, RAY_FLAG_NONE, 0xFF, reflect_ray);
        reflect_query.Proceed();

        if (reflect_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            // Look up the material of the reflected hit via instance custom index.
            uint hit_instance = reflect_query.CommittedInstanceID();
            uint hit_mat_idx = objects[hit_instance].material_index;
            MaterialData hit_mat = materials[hit_mat_idx];

            if (hit_mat.emissive_strength > 0.0) {
                // Hit an emissive object (neon tube or orb) — return its glow directly.
                reflected_colour = hit_mat.emissive * hit_mat.emissive_strength;
            } else {
                // Hit a non-emissive surface — evaluate wireframe blend.
                float2 hit_bary = reflect_query.CommittedTriangleBarycentrics();
                float bary_w = 1.0 - hit_bary.x - hit_bary.y;
                float hit_edge = min(hit_bary.x, min(hit_bary.y, bary_w));
                float hit_wire = smoothstep(0.0, REFLECTED_WIRE_WIDTH, hit_edge);

                float3 hit_pos = reflect_origin + reflect_dir * reflect_query.CommittedRayT();
                float3 hit_tube_colour = neonEmissiveColour(hit_pos);

                float3 hit_emissive = lerp(hit_tube_colour * TUBE_EMISSIVE_STRENGTH, float3(0.0, 0.0, 0.0), hit_wire);
                float3 hit_base = lerp(float3(0.0, 0.0, 0.0), hit_mat.base_colour, hit_wire);

                // Look up the reflected hit's indirect GI from its reservoir (one frame latent).
                // reservoir_previous stores last frame's data, so reproject through last
                // frame's view-projection to sample the correct pixel — using the current
                // frame's VP (as the previous implementation did) misses the mark whenever
                // the camera moves.
                float3 hit_indirect = hit_base * AMBIENT; // Fallback.
                float4 hit_clip = mul(camera.prev_view_projection, float4(hit_pos, 1.0));
                if (hit_clip.w > 0.0) {
                    float2 hit_ndc = hit_clip.xy / hit_clip.w;
                    float2 hit_uv = hit_ndc * float2(0.5, -0.5) + 0.5;
                    int2 hit_px = int2(hit_uv * float2(camera.screen_width, camera.screen_height));
                    if ((hit_px.x >= 0) && (hit_px.x < int(camera.screen_width))
                        && (hit_px.y >= 0) && (hit_px.y < int(camera.screen_height)))
                    {
                        uint hit_res_idx = uint(hit_px.y) * camera.screen_width + uint(hit_px.x);
                        Reservoir hit_res = reservoir_previous[hit_res_idx];
                        if (hit_res.M > 0) {
                            hit_indirect = hit_res.indirect;
                        }
                    }
                }

                reflected_colour = hit_emissive + hit_indirect;
            }
        }
    }

    // Canonical hybrid composite (per Bitterli et al. / Karis UE4 notes / LearnOpenGL IBL):
    // direct analytic lighting + Fresnel-weighted RT reflection + emissive + indirect. The
    // two specular terms cover different things — analytic spec evaluates the Cook-Torrance
    // BRDF at the selected emissive sample's direction (roughness-broadened lobe around that
    // single light), while RT reflection captures scene radiance at the mirror direction.
    // Slight overlap in the perfect-mirror limit is accepted as the standard hybrid-rendering
    // compromise; replacing one with the other blacks out pixels whose reflection ray misses.
    //
    // RTAO (ao_factor ∈ [0, 1]) multiplies only the Lambertian lobes — the indirect GI
    // diffuse bounce and the direct-lighting diffuse contribution. Specular (analytic and
    // RT reflection) and emissive are not attenuated by AO because AO represents small-scale
    // geometric occlusion of the diffuse irradiance integral, not of mirror reflections or
    // self-emitted radiance.
    float3 diffuse_term = (indirect_gi + direct_diffuse) * ao_factor;
    float3 colour = diffuse_term + direct_specular + emissive + reflected_colour * F_view;
    return float4(colour, 1.0);
}

// With vertex duplication for barycentrics, each triangle outputs 3 unique vertices.
// Max: 84 triangles × 3 = 252 vertices (within the 256 vertex output limit).
// Must match MAX_MESHLET_TRIANGLES in meshlet.hpp.
static const uint MAX_OUT_TRIANGLES = 84;
static const uint MAX_OUT_VERTICES = 252; // 84 × 3

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void meshMain(
    uint group_id : SV_GroupID,
    uint thread_id : SV_GroupThreadID,
    in payload TaskPayload task_payload,
    OutputVertices<MeshOutput, 252> out_vertices,
    OutputIndices<uint3, 84> out_triangles)
{
    uint meshlet_index = task_payload.meshlet_offset + group_id;
    MeshletDesc meshlet = meshlet_descs[meshlet_index];

    // Each triangle outputs 3 unique vertices (for barycentric coordinates).
    uint out_tri_count = min(meshlet.triangle_count, MAX_OUT_TRIANGLES);
    uint out_vert_count = out_tri_count * 3;
    SetMeshOutputCounts(out_vert_count, out_tri_count);

    // Load the object transform and material type.
    float4x4 model = objects[task_payload.object_index].model;
    float4x4 mvp = mul(camera.projection, mul(camera.view, model));
    uint mat_idx = objects[task_payload.object_index].material_index;

    // Barycentric coordinate lookup — vertex 0, 1, 2 of each triangle.
    static const float3 BARY[3] = {
        float3(1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0),
        float3(0.0, 0.0, 1.0)
    };

    // Each thread processes one or more triangles.
    // For each triangle, output 3 vertices with unique barycentrics.
    uint tris_per_thread = (out_tri_count + 63) / 64;
    for (uint t = 0; t < tris_per_thread; ++t) {
        uint tri_index = thread_id * tris_per_thread + t;
        if (tri_index < out_tri_count) {
            uint base = meshlet.triangle_offset + tri_index * 3;
            uint local_indices[3] = {
                uint(meshlet_triangle_indices[base + 0]),
                uint(meshlet_triangle_indices[base + 1]),
                uint(meshlet_triangle_indices[base + 2])
            };

            // Load all 3 vertices for this triangle.
            // Terrain uses flat shading: all 3 vertices share the same pre-computed normal.
            uint vert_base = tri_index * 3;

            float3 positions[3];
            float3 normals[3];
            float3 smooth_normals[3];
            float diagonal_flags[3];
            for (uint v = 0; v < 3; ++v) {
                uint global_idx = meshlet_vertex_indices[meshlet.vertex_offset + local_indices[v]];
                Vertex vtx = vertices[global_idx];
                positions[v] = float3(vtx.pos_x, vtx.pos_y, vtx.pos_z);
                normals[v] = float3(vtx.norm_x, vtx.norm_y, vtx.norm_z);
                smooth_normals[v] = float3(vtx.smooth_nx, vtx.smooth_ny, vtx.smooth_nz);
                diagonal_flags[v] = vtx.uv_x;
            }

            // Find which vertex is opposite the quad diagonal edge.
            // That vertex has uv_x = 1.0 (set by the terrain generator).
            // Adding its barycentric component to all 3 vertices pushes the
            // diagonal's interpolated value above the wireframe threshold,
            // effectively merging coplanar triangle pairs into quads.
            float3 diagonal_mask = float3(0.0, 0.0, 0.0);
            for (uint d = 0; d < 3; ++d) {
                if (diagonal_flags[d] > 0.5) {
                    diagonal_mask = BARY[d];
                    break;
                }
            }

            // Output vertices with diagonal-suppressed barycentrics.
            for (uint v = 0; v < 3; ++v) {
                float4 world_pos = mul(model, float4(positions[v], 1.0));

                MeshOutput output;
                output.position = mul(mvp, float4(positions[v], 1.0));
                output.barycentric = BARY[v] + diagonal_mask;
                output.world_position = world_pos.xyz;
                output.world_normal = mul((float3x3)model, normals[v]);
                output.material_index = mat_idx;
                output.prev_clip_pos = mul(camera.prev_view_projection, world_pos);
                output.smooth_normal = mul((float3x3)model, smooth_normals[v]);
                output.instance_id = task_payload.object_index;
                out_vertices[vert_base + v] = output;
            }

            // Triangle indices reference the duplicated vertices.
            out_triangles[tri_index] = uint3(vert_base, vert_base + 1, vert_base + 2);
        }
    }
}

// Sky colour at a given world-space ray direction. Lightweight gradient used as the miss
// fallback for refraction and reflection ray queries fired from fragTransparent — too cheap
// to be the canonical skybox but visually consistent (cool dark cyan-blue near horizon, even
// darker overhead). The full procedural skybox shader is not invoked here because that
// pipeline runs in a separate draw call against the depth buffer; we just need a sensible
// background-colour estimate for misses.
float3 transparentMissColour(float3 ray_dir)
{
    float horizon_t = saturate(ray_dir.y * 0.5 + 0.5);
    float3 horizon = float3(0.06, 0.10, 0.14);
    float3 zenith = float3(0.01, 0.02, 0.04);
    return lerp(horizon, zenith, horizon_t);
}

// Samples the unlit surface colour at a refraction/reflection ray hit. Reads the hit instance's
// material via CommittedInstanceID (which equals the C++ `instanceCustomIndex = entity index`
// set during TLAS build) and returns base_colour + emissive*strength. This is intentionally
// "look-up only", not a full Cook-Torrance + ReSTIR evaluation — recursive ReSTIR sampling
// would be prohibitively expensive (per-fragment ReSTIR reservoir lookups for every refracted
// pixel). The visual approximation is acceptable for a translucent surface: emissive geometry
// behind the glass shows its hot colour, dielectric geometry shows its base albedo dimly.
float3 sampleHitColour(uint hit_instance_id)
{
    uint hit_material_index = objects[hit_instance_id].material_index;
    MaterialData hit_mat = materials[hit_material_index];
    return hit_mat.base_colour + (hit_mat.emissive * hit_mat.emissive_strength);
}

// Inline ray query that traces along `ray_dir` from `origin`, skipping any hit whose
// instance ID matches `self_instance` (so a refraction ray fired from the front face of the
// glass tower does NOT immediately re-hit the glass tower's own back face). Requires the
// transparent BLAS geometries to be marked non-opaque so candidate hits go through Proceed()
// rather than auto-committing — see main.cpp glass + pillar BLAS build flags.
float3 traceTransparentRay(float3 origin, float3 ray_dir, uint self_instance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = ray_dir;
    ray.TMin = 0.001;
    ray.TMax = 1000.0;

    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(scene_tlas, RAY_FLAG_NONE, 0xFF, ray);

    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) {
            if (q.CandidateInstanceID() == self_instance) {
                continue; // Skip self-instance — front-face fragment refracting through its own back face.
            }
            q.CommitNonOpaqueTriangleHit();
        }
    }

    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        return sampleHitColour(q.CommittedInstanceID());
    }
    return transparentMissColour(ray_dir);
}

// Per-fragment opacity for the transparent surface. The ray-traced refraction explicitly
// samples what's behind the surface (so we don't strictly need destination bleed-through),
// but allowing a partial dst contribution makes the surface read as "thinner" / glassier.
// 0.7 → 70% of the explicit ray-traced result + 30% of whatever the opaque pass + skybox
// wrote underneath. Slight double-counting (dst already represents the world behind), but
// the visual is more pleasing than an alpha = 1 full-replacement.
static const float TRANSPARENT_OUTPUT_ALPHA = 0.7;

// Transparent fragment shader (Phase 8 Etape 40 sub-etape 40c). Real-time glass shader:
// Schlick Fresnel from material IOR, ray-traced refraction via Snell's law (HLSL `refract()`
// implements the canonical GLSL spec T = η·I − (η·(N·I) + √k)·N where
// k = 1 − η²·(1 − (N·I)²); incident I points INTO the surface and N points TOWARD I's
// origin so N·I is typically negative). TIR fallback to reflection when k < 0 (refract()
// returns the zero vector). Fresnel-weighted blend of refraction + reflection plus the
// surface's own emissive radiance.
//
// No reservoir reads or writes — transparents stay out of the ReSTIR temporal/spatial state
// so the opaque pass keeps full ownership of per-pixel reservoir slots.
//
// Output uses TRANSPARENT_OUTPUT_ALPHA (premultiplied) — the refraction ray query has
// explicitly captured what's behind the surface, but the partial alpha lets some of the
// destination (opaque pass + skybox) bleed through to soften the look from "explicit
// substitute" toward "tinted glass over a real background". The transparent pipeline is
// still in use (rather than the opaque one) so depth writes stay disabled and future
// overlapping transparents can blend correctly.
[shader("fragment")]
float4 fragTransparent(MeshOutput input) : SV_Target
{
    MaterialData mat = materials[input.material_index];

    float3 V = normalize(camera.camera_pos - input.world_position);
    float3 N_face = normalize(input.world_normal);

    // Orient the surface normal toward the viewer for the Snell convention. The mesh produces
    // outward face normals, but a transparent box is two-sided — we may be looking at a back
    // face from the camera's perspective when the camera is positioned past the surface. The
    // entering_surface flag also feeds the eta selection: ray going from air into the medium
    // uses η = 1/IOR; ray going from medium into air uses η = IOR.
    bool entering_surface = (dot(N_face, V) >= 0.0);
    float3 N = entering_surface ? N_face : (-N_face);
    float NdotV = saturate(dot(N, V));

    // Schlick Fresnel from material IOR. F0 = ((n-1)/(n+1))² (canonical form for air↔dielectric
    // boundary at normal incidence). Glass at IOR 1.5 gives F0 ≈ 0.04, the standard PBR value.
    float ior_ratio = (mat.ior - 1.0) / (mat.ior + 1.0);
    float F0 = ior_ratio * ior_ratio;
    float F = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);

    // Snell's law refraction direction. eta = n_from / n_to (incident to transmitted medium).
    // refract() returns the zero vector when total internal reflection occurs, which only
    // happens for medium→air (eta > 1) at angles past the critical angle.
    float3 I = -V;
    float eta = entering_surface ? (1.0 / mat.ior) : mat.ior;
    float3 T = refract(I, N, eta);
    bool tir = (dot(T, T) < 1e-6);
    float3 refract_dir = tir ? reflect(I, N) : T;

    // Origin offsets to avoid self-intersection at the originating surface. Refraction goes
    // INTO the surface so origin sits just beyond the surface in the −N direction; reflection
    // bounces OFF so origin sits just outside the surface in the +N direction. The skip-self
    // logic in traceTransparentRay handles the same-instance case but the offset still helps
    // numerical robustness on grazing rays.
    float3 refract_origin = input.world_position - (N * 0.001);
    float3 refract_colour = traceTransparentRay(refract_origin, refract_dir, input.instance_id);

    // Beer-Lambert-lite tint: refracted light passes through the medium and picks up the
    // material's base colour. A full Beer-Lambert would attenuate by exp(-σ·d) where d is the
    // path length inside the medium, but for a thin transparent box the linear tint is a
    // reasonable approximation and avoids needing a second ray hit to measure d.
    refract_colour *= mat.base_colour;

    // Reflection ray uses the same canonical reflect() (R = I − 2·(N·I)·N).
    float3 reflect_dir = reflect(I, N);
    float3 reflect_origin = input.world_position + (N * 0.001);
    float3 reflect_colour = traceTransparentRay(reflect_origin, reflect_dir, input.instance_id);

    // Fresnel-weighted composite + the surface's own emissive radiance (which always emits
    // regardless of whether light bounces or transmits). Energy-barrier pillar emission is
    // tuned moderate (1.5×) so it glows visibly but does not swamp the dielectric reflection
    // / refraction terms; clear glass (zero emissive) shows pure F·reflect + (1−F)·refract.
    float3 emissive_term = mat.emissive * mat.emissive_strength;
    float3 surface_colour = (F * reflect_colour) + ((1.0 - F) * refract_colour) + emissive_term;

    // Premultiplied-alpha output: the blend equation final = src + (1 − srcA) × dst then
    // composes a partial bleed of the destination over the explicitly traced result.
    return float4(surface_colour * TRANSPARENT_OUTPUT_ALPHA, TRANSPARENT_OUTPUT_ALPHA);
}
