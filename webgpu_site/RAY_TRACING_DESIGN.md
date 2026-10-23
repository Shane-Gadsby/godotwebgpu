# Ray and Path Tracing on WebGPU Forward+ — Design

**Status (2026-10-23): measured and decided, not started.** This document is the design for software ray tracing in
the WebGPU Forward+ renderer — the hybrid effects that are next (Tier A: ray-traced ambient occlusion and shadows)
and the full path tracer that is **saved here for later consideration** (§7). The research trail, the dated
measurements and the task status live in `webgpu_notes/TASKS.md` §2.34; this document holds the design itself, so it
does not go stale when the task entry closes. The measurement tool is `webgpu_tests/raytracing_probe/` (its README
explains the kernels and the numbers).

The one-paragraph version: WebGPU has no hardware ray tracing and will not for years, so everything here is a
compute shader traversing a BVH we build ourselves, in storage buffers. On an RTX 4080 SUPER through Chrome that
traversal does 1.9–2.3 billion incoherent rays per second at 1080p, enough for several rays per pixel per frame; on a
Mali-G615 phone it does 19 million, a hundred times less, enough for nothing. So ray tracing is a **desktop-class,
per-project opt-in** that switches itself off on mobile adapters, built once as a driver-agnostic `RendererRD` layer
so it also runs natively on Vulkan, Metal and D3D12, where upstream Godot has no ray tracing in any renderer either.

---

## 1. Constraints that shape the design

| constraint | consequence |
|---|---|
| **WebGPU has no ray tracing API.** [gpuweb #535](https://github.com/gpuweb/gpuweb/issues/535) is at "Milestone 4+" with no date; the Chrome lead (Oct 2025) said Dawn has nothing, extensions cannot add it, and bindless is a prerequisite that is itself years out. WGSL has no `rayQuery`, no acceleration-structure binding, no RT stages. | Software traversal in compute shaders over storage buffers. No SBTs, no hit shaders: every "shader" at a hit is a branch in our own kernel. |
| **No bindless.** 48 sampled textures and 16 storage buffers per stage on the adapters measured (Chrome exposes the adapter's limits, which the shell requests; `engine.js`). | One node buffer and one triangle buffer for the whole scene; materials at a hit through a material table plus a few texture arrays or an atlas, never per-material bindings (§5.4). |
| **Upstream's `RenderingDevice` RT API** (PR #99119, merged 2026-01-27) is hardware-only, Vulkan-only, and **used by no Godot renderer**. This driver stubs it (`drivers/webgpu/rendering_device_driver_webgpu.cpp:3676`) and reports both feature bits false. | Nothing to be compatible with. Design software-first; do not emulate the hardware API (§9, rejected alternatives). |
| **Godot materials are shaders.** Every `ShaderMaterial` is its own program; a compute kernel cannot evaluate arbitrary material code at a hit. | Geometry-only effects first (AO, shadows). Hit shading through a *standard-material subset* and radiance caches, with an explicit approximation for custom shaders (§5.4). |
| **Tint is the WGSL compiler.** Everything goes GLSL → SPIR-V → `spirv_preprocess::run_pipeline()` → Tint → WGSL → Dawn. Function-scope arrays, data-dependent loops and `bitcast` all survive it (the probe's traversal does); ray-query SPIR-V ops would not. | Write the traversal in plain GLSL compute, as an include. Expect no translation penalty: the probe's numbers match what a native stack traversal should do on that GPU. |
| **Pipeline creation blocks the GPU process** (`TASKS.md` §2.4). | Few kernels, created with `createComputePipelineAsync` through the existing `RD::set_pipeline_creation_async()` path, prewarmed at scene load, variants kept to a handful. |
| **No 64-bit atomics, no image atomics**; subgroups (32 on NVIDIA, 16 on Mali) and `shader-f16` available on both measured adapters. | CPU or 32-bit-key GPU builds; subgroup ballot/compaction available for wavefront scheduling; f16 for attribute storage and the denoiser. |
| **Depth textures must look like depth** (`CLAUDE.md`): a `texture2D` named without `depth` gets a blank fallback silently. | Any pass reading the depth prepass names the binding `depth_*` and reads it with `textureGather`/`texelFetch`. |

## 2. Goals and non-goals

**Goals**
1. Ray-traced ambient occlusion and ray-traced shadows (directional first, then omni/spot) in Forward+, pixel-exact where
   shadow maps and SSAO are approximate, as a project opt-in. Desktop-class GPUs only.
2. A single acceleration-structure layer (BLAS per surface, TLAS per frame) that every later effect reuses, built once,
   driver-agnostic, running natively too.
3. A measured budget: each effect states its cost per pixel from the probe and is checked by a test fixture.
4. The full path tracer designed well enough (§7) to be built when wanted, without redoing the thinking.

**Non-goals**
- Replacing rasterization (the NVIDIA fork's approach). Godot's 2D, UI, particles, custom material shaders and the
  whole Forward+ pipeline stay; rays are secondary visibility only, except in the offline reference mode of §7.
- Mobile. The phone measurement (§3) rules it out for this hardware generation; the feature is off there.
- Hardware RT. If WebGPU ever ships it, the traversal include gets a second implementation; nothing above it changes.
- Emulating upstream's hardware RT API inside the driver (§9).

## 3. Measured baseline

From `webgpu_tests/raytracing_probe/` (binned-SAH BVH2, leaves ≤ 4 triangles, stack traversal, timestamp queries, GPU
traversal verified equal to a brute-force CPU trace on every sampled ray). Scene: ground plane plus tessellated spheres
and boxes; geometry only, no materials, no instance transforms.

| device | resolution | triangles | primary (coherent, closest) | shadow (any hit) | bounce (incoherent, closest) | 1 spp of all three |
|---|---|---|---|---|---|---|
| RTX 4080 SUPER, Chrome/Dawn/Vulkan | 1920×1080 | 93 k | 0.39 ms · 5.3 Grays/s | 0.24 ms · 5.4 Grays/s | 0.54 ms · 2.3 Grays/s | **1.17 ms** |
| same | 1920×1080 | 945 k | 0.55 ms · 3.7 Grays/s | 0.33 ms · 3.8 Grays/s | 0.67 ms · 1.9 Grays/s | **1.55 ms** |
| Razr 60, Mali-G615 MC2, Chrome 155 | 1280×720 | 93 k | 20.7 ms · 45 Mrays/s | 14.3 ms · 40 Mrays/s | 29.6 ms · 19 Mrays/s | **64.6 ms** |

Mean traversal steps per ray: 14–17 primary, 16–20 shadow, 19–24 bounce; BVH depth 20–23. JavaScript build time 101 ms
for 93 k triangles and 749 ms for 945 k (a C++ build is several times faster).

**Discounts to apply to the desktop rows for a real frame:** ~1.5× for a two-level traversal (instance transform, a
second root), 2–3× for a scene whose BVH and textures do not sit in the 64 MB L2 the way the probe's 6–60 MB did, and
1.2–2× for material fetches and alpha tests at hits. Even at 4× total, one 1080p ray per pixel of the incoherent kind
costs ~2 ms on this GPU, and a coherent one ~1 ms.

**Budget arithmetic:** 1080p is 2.07 M pixels; a kernel at *N* Mrays/s costs 2074 / *N* ms. A 60 fps frame has 16.7 ms
and a hybrid effect may take 3–5 ms of it. Tier A at full resolution (1 AO ray + 1 shadow ray per pixel, both after the
discounts) is 3–4 ms; at half resolution with temporal accumulation under 1 ms. The phone cannot afford one quarter-
resolution AO ray per pixel (12 ms).

## 4. Architecture overview

```
CPU, per scene load / mesh change                 CPU, per frame                     GPU, per frame
─────────────────────────────────────────         ──────────────────────────────     ─────────────────────────────────
MeshStorage surface (CPU SurfaceData)  ──build──▶ BLAS cache (per surface, LOD 0)    [skinned/blend-shaped surfaces:
  positions, indices, normals, uv, uv2            ▲ export-time bake (optional)        refit kernel over skeleton.glsl's
                                                  │                                    output buffer → leaf AABBs → up]
RendererSceneCull visible instances ───────────▶ TLAS build (CPU, ~1k–10k instances)
  transform, mesh, layers, cast_shadow,           instance records + 2-level BVH   ──▶ TLAS/BLAS/triangle/attribute/
  material ids, alpha-test flag                   upload (one storage buffer each)      material buffers (storage)
                                                                                              │
Forward+ frame: depth prepass + normal_roughness ─────────────────────────────────────────────┤
  _pre_opaque_render:  RT AO kernel ──▶ RB_SCOPE_SSAO/RB_FINAL ─▶ ao_buffer (set 1 binding 27) ─▶ main pass (unchanged)
                       RT shadow kernel ──▶ shadow mask ─▶ sscs_buffer slot (binding 37) ─▶ light loop min()
  post-opaque:         RT reflections ──▶ RB_SCOPE_SSR ─▶ ssr_buffer (binding 35)                  [Tier B]
  temporal: motion vectors + history textures per effect, bilateral/variance blur, half-res upscale
```

Everything to the right of the TLAS is ordinary `RenderingDevice` compute: storage buffers, uniform sets, dispatches,
textures the existing passes already consume. The render graph tracks the hazards; on WebGPU barriers are no-ops anyway.

### 4.1 Where the code lives

- `servers/rendering/renderer_rd/effects/ray_tracing.{h,cpp}` — `RendererRD::RayTracing`: BLAS cache, TLAS build and
  upload, the kernels' pipelines, the per-effect entry points (`process_ao`, `process_shadows`, later `process_reflections`),
  settings. Modeled on `effects/ss_effects.*` and `environment/gi.*`.
- `servers/rendering/renderer_rd/shaders/effects/rt_trace_inc.glsl` — the traversal library (§5.3), included by every
  RT kernel; `rt_ao.glsl`, `rt_shadows.glsl`, later `rt_reflections.glsl`, `rt_path_trace.glsl`.
- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp` — calls into the effect from
  `_pre_opaque_render` beside `_process_ssao`/`_process_sscs`, and the flags that make the prepass produce
  `normal_roughness` when an RT effect is on (the SSAO/SSIL/SSR flags already do this).
- `drivers/webgpu/wgsl_precompile.py` — the new shaders join the precompile list so they ship baked; the shader corpus
  gets fixtures for them (the traversal loop is the first data-dependent loop of its kind in the engine's shaders).
- `scene/resources/mesh.cpp` / `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp` — a hook for BLAS
  invalidation on surface change; optionally the export-time bake next to the WGSL bake (§5.1).

## 5. The pieces

### 5.1 Acceleration structures

**BLAS per mesh surface.** Built on the CPU from the surface data `MeshStorage` already keeps (`mesh_get_surface()`:
positions and indices in the engine's vertex format — decode, do not read anything back from the GPU; `TASKS.md` §3
trap 54 is what a readback costs on this platform). Binned SAH (16 bins, leaves ≤ 4 triangles, children contiguous), the
probe's layout: 32-byte nodes (`bmin`, `left_first`, `bmax`, `count`), 48-byte triangles (`v0`, `e1`, `e2`) in leaf order,
plus a 4-byte original-index per triangle so attributes can be fetched. Depth capped at 44 under a 48-entry traversal
stack. LOD 0 only — rays do not benefit from screen-space LOD and a mismatch between the rasterized LOD and the traced
one shows as self-shadowing; the per-pixel bias handles the residual.

- *When:* lazily on first use by a visible instance, on a worker thread in threaded builds (the probe's JavaScript build
  is 100 ms per 100 k triangles; C++ is 3–5× faster). A frame may draw before its BLAS exists: the TLAS simply omits the
  instance that frame (the effect degrades to its fallback for those pixels, see §5.8).
- *Export-time bake:* the same builder run by the export plugin, the trees shipped in the pack next to the baked WGSL
  (`ShaderBakerExportPlugin` pattern, §4.23), keyed by surface hash. Removes the build from load time entirely; worth
  doing once Tier A is in use on a real project. Imported meshes could also bake at import.
- *Dynamic geometry:* skinned and blend-shaped surfaces already have their final positions in a storage-usage vertex
  buffer written by `skeleton.glsl` every frame. A **refit** kernel recomputes leaf AABBs from those positions and
  propagates upward one level per dispatch (depth ≤ 44 dispatches, or a single kernel with an atomic "both children
  done" counter — 32-bit atomics are enough). Topology is unchanged, so the tree stays valid; quality degrades slowly
  with large deformation, which is acceptable for characters. Rebuild instead when a surface is small (< 4 k triangles).
  `ImmediateMesh`, `CSG` and procedurally rebuilt `ArrayMesh`es invalidate their BLAS through the surface-change hook.
- *Memory:* 2N−1 nodes × 32 B + N × 52 B ≈ 116 B per triangle: 1 M triangles ≈ 116 MB, well inside the 2 GiB storage
  binding Chrome exposes on desktop; a project setting caps the total and the TLAS drops the farthest instances first.
- *Later:* a 4-wide BVH (or Ylitie's compressed wide BVH) if traversal-bound; the probe's 14–24 steps per ray at 2–5
  Grays/s says the BVH2 is not the bottleneck yet.

**TLAS per frame.** Built on the CPU over the culled `RenderGeometryInstance` list — the renderer already has
transforms, AABBs, layers and flags for every visible instance; a thousand instances build in well under a millisecond,
ten thousand in a few. Instance record (64 B): world→object 3×4 matrix, BLAS node/triangle offsets, material table index,
flags (`cast_shadow`, alpha-tested, double-sided, `layer_mask` low bits as the visibility mask). A second BVH2 over
instance AABBs becomes the top level; the traversal visits instance leaves, transforms the ray into object space and
continues in the BLAS (two-level, one stack). MultiMesh instances enter as individual records (their transform buffer is
already a storage buffer the CPU owns); GPU particles are omitted from Tier A (they would need a GPU-side TLAS append)
and noted as a known gap.

### 5.2 Scene data the kernels need

| data | source | GPU layout |
|---|---|---|
| positions, indices | `SurfaceData` | BLAS triangle buffer (`v0,e1,e2`), index-to-original map |
| normals, tangents, UV, UV2 | `SurfaceData` attribute streams | one compact `f16` attribute buffer per surface (16 B per vertex: normal oct-encoded 4 B, uv 4 B, uv2 4 B, tangent sign + pad), concatenated; fetched by original index for interpolation |
| per-instance | `RenderGeometryInstance` | instance records (above) |
| materials | `StandardMaterial3D`/`ORMMaterial3D` parameters; custom shaders approximated | material table (§5.4) |
| lights | `LightStorage` | the Forward+ light buffers as they are (`directional_lights`, `omni_lights`, `spot_lights`, area lights) |
| sky | `Sky` radiance cubemap | sampled for miss shading in §7 and for Tier B/C |

### 5.3 The traversal include (`rt_trace_inc.glsl`)

```glsl
struct RTHit { float t; uint instance; uint prim; vec2 bary; uint steps; };
bool rt_trace_closest(vec3 ro, vec3 rd, float tmin, float tmax, uint mask, out RTHit hit);
bool rt_trace_any    (vec3 ro, vec3 rd, float tmin, float tmax, uint mask);           // shadows, AO
RTSurface rt_resolve (RTHit hit);   // world position, interpolated normal/uv/uv2, material index, flags
```

Plain GLSL 450 compute, no extensions. The probe's kernel is the reference implementation: slab test on both children,
near-first ordering, far child pushed, Möller–Trumbore in leaves, `t` as the running `tmax`. Additions over the probe:
the instance level (object-space ray per instance, `tmax` kept in world units by using an orthonormal-plus-scale
transform or rescaling), `mask` against the instance flags, and alpha-tested leaves — a hit on an instance flagged
alpha-tested fetches UV + the material's alpha texture and continues if cut out (one extra texture fetch; stochastic
alpha-hash for translucency later). `rt_resolve()` is where attribute interpolation lives so kernels never touch the
layouts. The same include runs the editor's lightmapper-style tooling if ever wanted; it is the one place the data
layout is known.

### 5.4 Shading at a hit without bindless

The hard problem for everything past Tier A. Options, all usable together:

1. **Nothing** — AO and shadows need only geometry and the alpha mask. This is why Tier A comes first.
2. **Material table + texture arrays.** `StandardMaterial3D`/`ORMMaterial3D` parameters packed per material (albedo,
   metallic, roughness, emission, normal scale, texture indices), textures copied at scene load into a few
   `texture2DArray`s of fixed sizes (e.g. 512² and 2048², mip-mapped; BC/ETC2/ASTC kept as-is where the array format
   allows) with a per-material index and a scale/offset. Budget: 3–6 sampled textures per stage of the 48. Custom
   `ShaderMaterial`s get the parameters their uniforms expose with Godot's standard names and the material's `albedo`
   texture if it has one; otherwise a flat gray with the right roughness — documented, and visible only in bounce light.
3. **Radiance caches at the hit.** For GI and reflections the color *leaving* a hit matters more than its material. The
   lightmapper's answer is a UV2 atlas of albedo/emission; the modern answer is a surface cache in lightmap/UV2 space
   updated incrementally (a few thousand texels per frame, lit by the same NEE as the kernel). Reflection probes and
   SDFGI's cascades already hold coarse radiance for the far field. Tier B uses the screen (below) plus probes/SDFGI; Tier C
   adds the surface cache.
4. **Screen-space reprojection.** A hit that lands on-screen takes the current frame's lit color (what SSR does) and only
   off-screen hits use 2–3. Cheap and sharp; the usual disocclusion/thickness checks apply.

### 5.5 Lights

The Forward+ light buffers are bound as they are. Shadow rays are generated toward: the directional light's disc
(`angular_distance` → cone sample; `shadow_blur` ignored), omni/spot lights' sphere (`size` → sphere sample, the same
parameter the raster PCSS uses), area lights' rectangle (uniform or solid-angle sampling of the quad the raster integrates
with LTC in `lm_area_lights_inc.glsl`/`light_process_area`). One ray per light per pixel per frame with a blue-noise
sequence and temporal accumulation gives soft shadows for free; `shadow_opacity` and the light's `shadow` flag are
honored; lights without shadows cast no rays. For §7, next-event estimation picks lights by power with a CDF (a light
BVH later), and the sky is a light.

### 5.6 Integration into the Forward+ frame

- **Inputs:** the depth prepass and `normal_roughness` (requested the way SSAO/SSIL/SSR request it; MSAA → resolved
  copies), the camera and the per-light data already in `scene_data`, motion vectors for temporal reuse
  (`RB_TEX_VELOCITY`, which TAA/FSR2 and compositor effects already can turn on).
- **RT AO** runs where `_process_ssao` runs and writes `RB_SCOPE_SSAO`/`RB_FINAL` (R8, storage-capable — the SSAO path
  already settles that on WebGPU), so the scene shader's `ao_buffer` read, the AO light affect/channel settings and the
  SSAO debug draw all work unchanged. Half-resolution by default with SSAO's upsample pattern.
- **RT shadows** write a screen-space shadow mask with one layer per shadowed directional light (omni/spot: a layer per
  light up to a cap, like `sscs_index`), consumed where `sscs_buffer` is today (`scene_forward_clustered.glsl:2655`,
  `shadow = min(shadow, sscs_shadow)`). A second mode skips the shadow-map lookup altogether for lights the mask covers,
  saving the atlas passes (6 per omni light, `TASKS.md` §2.31 follow-up 1); transparent and non-prepass geometry still
  need the atlas, so mode one ships first.
- **RT reflections (Tier B)** run post-opaque where `_process_ssr` runs and write `RB_SCOPE_SSR`, consumed as
  `ssr_buffer`. Roughness cut-off from the material; SSR's existing blend-in logic stays.
- **Compositor effects** need nothing new: `COMPOSITOR_EFFECT_FLAG_NEEDS_ROUGHNESS` and the depth/color access flags
  already exist, and a `CompositorEffect` can bind the TLAS/BLAS buffers through an accessor if scripts want to trace.
- **Multiview/XR:** not supported in the first version (per-view kernels later; the buffers are per view already).

### 5.7 Temporal accumulation and denoising

Every effect traces 1 ray per pixel (per light) per frame and relies on reuse: reprojection with motion vectors and
depth/normal rejection into a history texture (the TAA pattern), then a bilateral/variance-guided spatial pass. AO: 4–8
frames of history plus SSAO's existing blur. Shadows: the standard "penumbra-aware" temporal + spatial denoiser (history
length by variance, spatial radius from penumbra estimate). Reflections: hit-distance-aware reprojection, roughness-scaled
blur. GI (Tier C): SVGF or a ReSTIR GI reservoir pass. All at half resolution with a depth-aware upsample until measured
otherwise.

### 5.8 Platform gating, settings and fallbacks

- `rendering/raytracing/enabled` (default off), `rendering/raytracing/ambient_occlusion`, `rendering/raytracing/shadows`,
  `rendering/raytracing/half_resolution` (default on), `rendering/raytracing/max_triangles` (memory cap),
  `rendering/raytracing/mobile_fallback` (default: disabled on mobile-class adapters).
- **Adapter class** at startup: the device vendor (`RenderingDevice::get_device_vendor_name()`; the WebGPU driver fills
  `device_info.vendor` from the adapter) and `DeviceType`; ARM/Qualcomm/Imagination integrated → mobile-class → RT off,
  the raster fallbacks run (SSAO, shadow maps, SSR). Optionally a 50 ms throughput check on first use (a tiny trace
  dispatch timed with a timestamp query) that disables RT below a Mrays/s floor, so an unknown weak desktop GPU also
  falls back. The decision prints once with `print_verbose` (`WebLogger` rules: not a warning, it is by design).
- **Per-pixel fallback:** pixels whose instance has no BLAS yet (building) or whose ray budget was exceeded take the
  raster effect's value for that frame.
- **Export:** the web export plugin warns once when RT is enabled in a project that exports for mobile presets.

## 6. Tiers

| tier | what | needs | desktop cost (projected) | acceptance |
|---|---|---|---|---|
| **A** | RT AO; RT directional shadows; then omni/spot RT shadows | §5.1–5.3, §5.6, §5.7, §5.8; alpha mask only | AO 0.5–1 ms half-res, shadows 1–2 ms per light full-res | Forward+ matrix fixtures `rt_ao`, `rt_shadows` (delta vs off, and web vs **native** parity — the same software pass runs natively, so `run_forward_plus.mjs --parity` applies); a many-spp brute-force render as the one-time truth; `perf_guard` budget |
| **B** | RT reflections replacing/augmenting SSR | §5.4 options 2–4, hit-distance denoiser | 2–4 ms full-res, 1 ray per pixel | fixture `rt_reflections` vs native parity; mirror/rough sphere scenes vs path-traced truth |
| **C** | Real-time path-traced GI (1 spp, denoised) as an alternative to SDFGI | §5.4 option 3 (surface cache), §5.5 NEE, SVGF/ReSTIR | tracing 2–4 ms + denoiser 2–3 ms | Cornell-style fixtures vs §7's converged render |
| **D** | The full path tracer (§7): offline/progressive reference, Movie Maker | everything above, wavefront scheduler | no frame budget | its own convergence tests; agreement with Tier A–C on their own terms |

Effort (one person, from the probe's state): the acceleration-structure layer ~1 week; Tier A 2–3 weeks including it
and the fixtures; Tier B 2–3 weeks; Tier C open-ended (the denoiser); Tier D ~2 weeks for the standard-material subset
once A exists, more for materials beyond it.

## 7. The full path tracer — saved for later consideration

What "full ray/path tracing" means here: every pixel's color computed by tracing paths — primary visibility, direct
light by next-event estimation, indirect light by BSDF-sampled bounces, the sky as the environment — with no raster
lighting involved, converging progressively. Two uses, one design: **(a)** a reference/offline renderer (Movie Maker at N
samples per frame, editor stills, ground truth for Tiers A–C), and **(b)** a real-time mode at 1 spp with a denoiser,
which is Tier C with the primary ray added. Both are desktop-only.

### 7.1 Why not replace rasterization

NVIDIA's `nvidia-pt-dlss` fork (GDC 2026) replaces rasterization outright and leans on DLSS Ray Reconstruction. That is
not available here (no DLSS on the web, no bindless, and Godot's material shaders cannot be evaluated at hits), and it
would discard 2D, UI, particles, custom shaders and the whole Forward+ pipeline. The design keeps the raster frame for
primary visibility and uses the G-buffer (depth, normal/roughness, albedo and emission reconstructed from the material
table or a thin extra prepass output) as the first path vertex. The offline mode can trace the primary ray instead, for
exact anti-aliasing and depth of field.

### 7.2 Scheduler: wavefront, not megakernel

A megakernel (the probe's shape: one thread follows one path to the end) is simplest but divergent: after two bounces
most lanes are idle in a subgroup. On WebGPU with 16–32-wide subgroups and no persistent-thread tricks, the **wavefront**
layout is the better fit and maps onto the engine's dispatch model (one compute list, several dispatches, the render
graph between them):

```
generate  : camera (or G-buffer) → path state buffer (origin, dir, throughput, pdf, rng, pixel)
extend    : trace closest hit for every live path                      ← the probe's kernel
shade     : resolve hit → material → NEE light sample + shadow ray request → BSDF sample → next ray or terminate (RR)
shadow    : trace any-hit for every shadow request, add contribution     ← the probe's any-hit kernel
compact   : subgroup ballot + prefix sum over live paths (indirect dispatch for the next extend)
repeat    : up to max_bounces (default 4; 8 offline)
accumulate: per-pixel running mean (f32 RGBA), sample count; temporal reprojection in real-time mode
```

Path state ~64 B per path, 1080p ≈ 130 MB for one path per pixel; sort-free (coherence from compaction alone is enough
at these ray rates; material sorting later if shading dominates). Megakernel stays as the debug/reference mode because it
is trivially correct.

### 7.3 Materials and BSDFs

Godot's PBR as the raster evaluates it in `scene_forward_lights_inc.glsl`'s `light_compute()` — Lambert or Burley
diffuse, GGX specular with Schlick Fresnel and `V_GGX`, clearcoat, anisotropy, rim, backlight, subsurface approximation —
ported to a sampling form: importance-sample a lobe by its albedo weight (diffuse cosine, GGX VNDF), evaluate `f·cos/pdf`,
MIS with the light sample. `StandardMaterial3D` is covered fully (including transparency via alpha-hash, refraction with a
scalar IOR, emission); custom `ShaderMaterial`s use the approximation of §5.4 and are flagged in the output so a user
knows which objects are approximate. Textures through the material table + texture arrays; normal maps applied at hits
(tangent frames from the attribute buffer).

### 7.4 Lights and environment

NEE over all Godot light types as in §5.5, chosen by a power CDF (a light BVH when scenes have hundreds of emitters),
with MIS against BSDF sampling. Emissive surfaces: BSDF-sampled only at first (correct, slower to converge), explicit
emissive-triangle sampling later. Sky: the `Sky` radiance cubemap (or the procedural sky evaluated directly) as an
infinite light with an importance map built from the cubemap. Volumetric fog and participating media are out of scope.

### 7.5 Sampling, convergence, denoising

PCG per pixel/sample/bounce (the probe's), Owen-scrambled Sobol later for stills. Offline: accumulate until a target
spp or a variance threshold, then write. Real time: 1 spp + ReSTIR DI for direct light and a ReSTIR GI or SVGF pass for
indirect, with the TAA motion vectors; expect the denoiser to be the larger part of the work and of the frame.

### 7.6 Where it plugs in

- **Offline:** `RendererSceneRenderRD` gains a render mode (viewport debug draw or a project/viewport setting
  `rendering/raytracing/path_tracing`) that replaces the opaque+sky+transparent passes with the wavefront loop and writes
  the result into the color buffer before tonemapping and post-effects, so Movie Maker (`--write-movie`, `MovieWriter`)
  records it unchanged with `spp_per_frame` from a setting. 2D/UI composite on top as today.
- **Editor:** the same mode behind a viewport toggle for a progressive preview when the camera is still.
- **Real time:** Tier C's integrator *is* this loop with the G-buffer as vertex 0 and `max_bounces` = 1–2.

### 7.7 Projected cost (from the probe, with the §3 discounts)

Desktop, 1080p: one path of 4 bounces ≈ 4 extend + 4 shadow traces ≈ 4×(0.55+0.33)×(2–4) ≈ 7–14 ms per sample, so
**~70–140 samples per second** → a 1024-spp still in 7–15 s, a 64-spp Movie Maker frame in 0.5–1 s, and 1 spp real time
within a frame budget only with 1–2 bounces and the denoiser doing the rest. Phone: not applicable.

### 7.8 What would make this worth building

A project that needs offline-quality stills or video from the web engine (the Movie Maker use), or Tier A–C needing a
ground truth that is not a Vulkan rasterization. Until then it stays a design; the acceleration-structure layer, the
traversal include, the material table and the light sampling built for Tiers A–B are three quarters of it.

## 8. Testing

- **Unit:** `webgpu_tests/raytracing_probe/` (traversal vs brute force; throughput; both devices). Extend it with the
  two-level traversal and alpha test once the include exists, by pointing it at the engine's GLSL through the corpus
  compile step.
- **Shader corpus:** fixtures for every RT kernel (the first data-dependent loops in the engine's shader set).
- **Forward+ matrix:** `rt_ao`, `rt_shadows`, later `rt_reflections` fixtures — delta vs off, and **web vs native parity**:
  the effect is driver-agnostic, so the native Vulkan render of the same fixture is the reference, as for every other
  feature (`run_forward_plus.mjs --parity`; `TASKS.md` §3 trap 28 on making that comparison trustworthy).
- **Truth:** one converged many-spp render per fixture from §7's megakernel mode (or the probe's brute-force path) to
  validate the effects' *correctness* once; parity then guards regressions.
- **Perf:** `perf_guard` budgets for the new dispatches and bytes; `gpu_time.mjs --passes` for the per-kernel cost.
- **Gating:** a smoketest scene that enables RT and asserts it is off on the swiftshader adapter and on the phone.

## 9. Rejected alternatives

- **Emulate upstream's acceleration-structure API inside the WebGPU driver and lower GLSL `rayQueryEXT` in a SPIR-V
  pass.** It would let future upstream ray-query shaders run unchanged, but Tint has no ray-query ops, glslang's output
  uses `OpTypeRayQueryKHR`/`OpRayQuery*`, and lowering those to a traversal is a DXR-fallback-layer-sized project — for
  shaders upstream has not written. Revisit if upstream ships ray-query renderer code.
- **Wait for WebGPU hardware RT.** No date, bindless first; years.
- **A uniform grid like the lightmapper's.** Simple and good for a static bake; poor for dynamic scenes and instancing.
  The lightmapper's grid stays where it is.
- **Replacing rasterization** (§7.1).

## 10. Risks and open questions

- The discounts in §3 are estimates; the first engine-side measurement (Tier A on a real project) replaces them.
- Material approximation for custom shaders is a visible compromise in bounce light (Tiers B–D), not in Tier A.
- Skinned-mesh refit quality under large deformation; rebuild thresholds to tune.
- GPU particles and procedural geometry without a CPU copy are gaps until a GPU-side TLAS append exists.
- Memory on large scenes (116 B/triangle); the cap and far-instance dropping need testing on the user's projects.
- Pipeline creation stalls on first use (`TASKS.md` §2.4): the kernels must go through the async path and be prewarmed.
- The adapter-class gate has false positives (a strong integrated GPU) and negatives (a weak discrete one): the
  throughput check covers both.

## 11. Decisions log

- 2026-10-23 — Software RT only; nothing hardware-dependent (WebGPU spec state, upstream state verified).
- 2026-10-23 — Desktop probe: 1.9–2.3 Grays/s incoherent → Tier A at full resolution is viable; build it.
- 2026-10-23 — Phone probe: 19 Mrays/s → RT is desktop-only, off on mobile-class adapters.
- 2026-10-23 — Full path tracer designed (§7) and deferred; the hybrid tiers come first.

## 12. References

- `webgpu_notes/TASKS.md` §2.34 (research trail, measurements, status), §2.31 (WebGPU-limited gaps), §3 trap 54.
- `webgpu_tests/raytracing_probe/README.md` — the probe, how to read it, how to run it on the phone.
- gpuweb issue #535 (ray tracing in WebGPU); Godot PRs #99119, #122051 (RenderingDevice RT API); godot-proposals
  discussion #5162; NVIDIA `nvidia-pt-dlss` fork (GDC 2026); Unity TrueTrace (compute path tracer with CWBVH, ReSTIR GI,
  ASVGF); WebRTX (compute RT layer for WebGPU); Ylitie et al. 2017 (compressed wide BVH); Laine, Karras, Aila 2013
  (megakernels considered harmful — wavefront path tracing).
