# WebGPU Rendering Driver for Godot 4.6

A `RenderingDeviceDriver` / `RenderingContextDriver` implementation targeting
WebGPU via Emscripten's **emdawnwebgpu** port (Dawn). This enables Godot's
Forward+ and Mobile renderers to run in the browser.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│  Godot RenderingDevice (servers/rendering/)             │
│    ↕ RenderingDeviceDriver interface                    │
├─────────────────────────────────────────────────────────┤
│  RenderingDeviceDriverWebGPU   (this driver)            │
│    • Buffers, Textures, Samplers, Pipelines, Draw calls │
│    • Push constant ring buffer emulation (group 3)      │
│    • Subpass flattening (each subpass → render pass)     │
│    • SPIR-V → WGSL translation (SPIRV-Cross, linked in) │
├─────────────────────────────────────────────────────────┤
│  RenderingContextDriverWebGPU                           │
│    • Device import from JS pre-initialized GPUDevice    │
│    • Surface creation from HTML canvas (#canvas)        │
│    • Swap chain management via WGPUSurfaceTexture       │
├─────────────────────────────────────────────────────────┤
│  emdawnwebgpu (Emscripten port)                         │
│    • Dawn WebGPU C API → browser WebGPU JS API          │
└─────────────────────────────────────────────────────────┘
```

### Files

| File | Lines | Purpose |
|------|-------|---------|
| `rendering_device_driver_webgpu.cpp/h` | ~5250 | Main driver: buffers, textures, pipelines, draw, compute |
| `rendering_context_driver_webgpu.cpp/h` | ~290 | Device bootstrap, surface/swap chain management |
| `rendering_shader_container_webgpu.cpp/h` | ~480 | Shader container format (SPIR-V storage, baked WGSL, baked reflection) |
| `webgpu_objects.h` | ~320 | GPU object wrappers (WGBuffer, WGTexture, WGShader, etc.) |
| `spirv_preprocess.cpp/h` | ~5100 | SPIR-V preprocessing passes before conversion, and `run_pipeline()` (which validates its input first), the one definition of their order |
| `spirv_opt_passes.cpp/h` | ~1060 | Passes that restructure control flow or add types, written against SPIRV-Tools' optimizer IR: texture-array expansion, the sampler LOD bias, lowering of constructs WGSL has no direct form for |
| `spirv_binding_layout.h` | ~80 | Where expanded array elements and the SamplerLodBias block bind; shared by the passes and the driver |
| `spirv_lite_reflect.cpp/h` | ~220 | Raw SPIR-V reflection with no SPIRV-Tools dependency, so the editor-side baker can run it too |
| `spirv_cross_wrapper.cpp/h` | ~130 | SPIRV-Cross's WGSL backend on preprocessed SPIR-V: options, vertex-stage `read_write` demotion, exceptions caught |
| `spirv_to_wgsl.cpp/h` | ~140 | Runtime entry point: `run_pipeline()` + the wrapper for anything not precompiled or baked |
| `wgsl_text_scan.cpp/h` | ~1430 | Everything the driver and baker read out of converted WGSL, parsed structurally, and the three text rewrites (std-only, tested natively by `webgpu_tests/wgsl_text_scan`) |
| `wgsl_cli/` | — | `wgsl_convert_cli`, the native host tool (precompile, export bake, debugging) and its fuzzers (`wgsl_cli/fuzz/`) |
| `pixel_formats_webgpu.h` | ~710 | Godot DataFormat → WGPUTextureFormat mapping table |

## Key Design Decisions

### Push Constant Emulation
WebGPU has no push constants. Emulated via a **256 KB ring buffer** (read-only
storage, binding 120 in group 3) with 256-byte aligned slots and dynamic
offsets. Each draw advances the ring offset; dirty-state tracking skips
unchanged data. Bind group created once and reused with dynamic offsets.

### Subpass Flattening
WebGPU has no subpasses. Each Godot subpass becomes a separate
`WGPURenderPassEncoder`. Attachment load/store ops are set per-pass based on
Godot's subpass configuration.

### Shader Translation
GLSL → SPIR-V (glslang, at build time) → WGSL (SPIRV-Cross's WGSL backend,
linked in as C++). SPIR-V is preprocessed in C++ before conversion: combined image-samplers
are split into separate texture + sampler bindings, push constant blocks are
rewritten to storage buffer references at binding 120, and various other
fixups (depth image flags, position Y negation, point size stripping) are
applied. SPIRV-Cross (`thirdparty/spirv-cross-wgsl`, patched locally, never
upstreamed) reports an unsupported shader by throwing, so its translation units
are built with `-fwasm-exceptions` in their own SCons environment (the link gets
the flag too) and `spirv_cross_wrapper.cpp` catches; the rest of the engine
stays `-fno-exceptions`. A throw returns no WGSL and the stage fails to convert
like any other error. Tint was the converter until 2026-10-25 (TASKS.md §4.36).

`run_pipeline()` first validates its input with SPIRV-Tools' validator
(`spirv_opt_passes::validate_module()`, Vulkan 1.2 rules, ~2.3 ms a module on
average, 26 ms on the 288 KB Forward+ uber shader) and rejects a malformed
module with `invalid SPIR-V, not converted: ...`, because SPIRV-Tools' optimizer
asserts on one. The engine's own runtime conversion (`spirv_to_wgsl.cpp`) opts
out — its input is glslang's output, and on the web the check would be pure
load time; `wgsl_convert_cli` and the fuzzers validate. Every pass that runs
the optimizer (`inline_opaque_functions`, `eliminate_dead_resources`,
`eliminate_local_single_block_vars`, and the three in `spirv_opt_passes.cpp`)
first runs `check_ir_preconditions()`, a linear scan for what the IR loader
asserts on; it is not a validator, and inside `run_pipeline()` it only fires
when an earlier raw pass broke the module (`WebGPU: <pass>: skipped, the module
is malformed: ...`). `rewrite_copy_logical` keeps an `OpCopyLogical` it would
expand past 65532 elements or 16 MB of words (a valid 1e8-element copy once took
6.2 GB).

`spirv_preprocess::run_pipeline()` is the only place the passes' order is
written down; the runtime driver, `wgsl_convert_cli` (and so the export-time
baker and the build-time precompile) and the fuzzers all call it. Three of the
passes live in `spirv_opt_passes.cpp`, because they restructure control flow:

- **Texture arrays** (`expand_binding_arrays()`): WGSL has no array of
  textures Chrome accepts, so `uniform texture3D t[8]` becomes eight bindings
  (`spirv_binding_layout.h` says where), and every dynamically indexed read a
  switch over the index. Expansion is budgeted against the per-stage texture
  limit -- 16, or 48 for a module already past 16, which only runs where
  Forward+ runs -- and an array that does not fit is collapsed to element 0
  as before, with a warning when its uniform set actually holds more.
- **Sampler LOD bias** (`apply_sampler_lod_bias()`): WebGPU samplers have no
  mip bias, so the scene shaders' render-pass samplers' bias (the viewport's
  `texture_mipmap_bias`, TAA/FSR's sharpening) is passed as data
  (`SamplerLodBias`, set 1 binding 40) and added to implicit-LOD samples.
- **What WGSL has no form for** (`lower_wgsl_unsupported_ops()`, named for
  Tint, which aborted on them): `isnan`/`isinf`, `textureQueryLod`,
  `POINT_COORD` and `early_fragment_tests` used to crash the whole page from a
  material; they are rewritten into what the converter accepts.
  (`gl_PointCoord` becomes `vec2(0.5)`, which only the non-emulated branch
  reads: WebGPU has no point size, so `SUPPORTS_POINT_SIZE` is false and the
  scene shaders draw each point as a quad with an interpolated coordinate. A
  material that reads `POINT_COORD` without writing `POINT_SIZE` used to skip
  that quad expansion and draw nothing, on D3D12 as well; the scene shader
  includes now define `POINT_SIZE_USED` for it.)

### Shader Precompilation
Three tiers, checked in order:

1. **Export-time bake** — with `shader_baker/enabled` on (the default for Web
   presets), `ShaderBakerExportPlugin` walks every `ShaderRD` the engine
   embeds *and* every material shader reachable from the exported resources,
   and stores each stage's WGSL inside that stage's own shader container in
   the `.pck`. The runtime reads it back by object identity, so there is no
   hash lookup to drift. Since specialization constants became WGSL overrides
   (below), one baked base module covers every value combination.

   Three things make this actually complete on WebGPU, each of which was a real
   gap once (`webgpu_notes/TASKS.md` Tasks 31–34):

   - **The bake runs with the target's capabilities, not the editor's.**
     Engine code derives `#define`s and shader-group choices from
     `RenderingDevice::has_feature()`, so baking on the editor's Vulkan device
     produced SDFGI, volumetric-fog and clustered variants the WebGPU runtime
     never asks for. The platform plugin installs the target's answers for the
     duration of the bake (`get_target_feature_overrides()`), and every group is
     baked rather than only the ones the editor happened to enable.
     **The answers are scoped** (2026-10-26, `webgpu_notes/TASKS.md` §4.43, §3
     trap 73): `shader_bake_feature_override_set()` installs the map for the
     export, but `has_feature()` consults it only on a thread inside an
     `RD::ShaderBakeFeatureOverrideScope`, which the baker opens in its own
     callbacks (`_begin_customize_resources`, `_customize_resource`,
     `_customize_scene`). The editor draws frames between export steps, and
     those keep the editor device's answers; before the scope, FSR 2 in the
     editor's viewport bound storage buffers to its image-atomics shaders on
     every such frame. `shader_bake_feature_override_is_active()` still means
     "a bake for an overriding target is running", in or out of a scope. A
     capability-dependent variant must be a shader group with
     `set_group_bake_requirements()` (`fog.cpp`, `fsr2.cpp`) or refreshed by
     `ShaderRD::add_general_defines_refresh_callback()` (`fsr.cpp`); a variant
     list built from `has_feature()` once at construction bakes the editor's
     flavor under cache keys the target never looks up.
   - **Every live shader version is baked**, not just those an exported resource
     or scene leads back to. Material versions are non-embedded, and a material
     created during the first frame — with no resource path at all — is reachable
     by no walk over resources or scenes.
   - **`res://` is searched before `user://`** when loading the shader cache. The
     baked cache carries WGSL; one written by the running game does not, so a
     single earlier run could otherwise shadow the bake permanently.

   How it runs (TASKS.md §4.23): every variant is compiled to SPIR-V on the
   worker pool, then `webgpu::WgslBakeBatch` (`wgsl_bake_subprocess.cpp`)
   converts all stages at once — deduplicated by SHA-256, screened for the
   constructs WGSL cannot express, split into ~4 chunks per core of
   `wgsl_convert_cli --batch` started with `posix_spawn()` — and only then are
   the containers built. Converted WGSL is cached under
   `<OS cache>/godot/webgpu_wgsl_bake/v1-<sha256 of wgsl_convert_cli>/`, keyed
   by the SPIR-V's SHA-256, so it survives engine rebuilds and a converter
   rebuild invalidates it; `WEBGPU_BAKE_CACHE=0` turns it off. On web the containers
   ship as `<name>.shaders.pck` next to the main pack (preset
   `shader_baker/separate_pack`): the shell preloads it (`config.shaderPack`)
   and `ProjectSettings::_setup` mounts it. The shell/mount halves live in the
   template, so a template older than that change loads a game with no shader
   cache at all (`baked: 0`, no mismatch warning); the exporter warns when the
   template's `index.js` has no `shaderPack`.
2. **Build-time table** — `wgsl_precompile.py` bakes the engine's own
   ubershaders into `wgsl_precompiled.gen.h` during `scons ... webgpu=yes`.
   Keyed by SPIR-V hash, so it only hits when the engine's glslang output
   matches what the table was generated from. `SCsub` re-bakes it when a
   preprocessing-pass source, an engine `.glsl`, anything under vendored
   SPIRV-Cross/SPIRV-Tools/`wgsl_cli`, the FSR 1/2 shader sources under
   `thirdparty/amd-fsr{,2}`, `bin/wgsl_convert_cli` itself or glslang's
   version changes (a fingerprint `Value`, ~30 ms per scons read);
   `./build-linux.sh --force-shader-rebake` deletes the header to force one.
   The bake runs glslangValidator and `wgsl_convert_cli --batch` across all
   cores (~3 s for the 276 engine modules); `WGSL_PRECOMPILE_JOBS=N` caps it.
3. **Runtime conversion** — anything that missed, translated in the browser on
   demand. This is what an export with baking turned off uses for everything,
   and it is the only route for a shader that does not exist at export time
   (`Shader.new()` + `set_code()` at runtime).

> **The editor and the export template must be built from the same commit.**
> Shader cache hashes fold in `GODOT_VERSION_HASH` (`ShaderRD::setup()`), so if
> the editor that exports and the template that runs come from different commits,
> *every* hash differs and not one baked shader can be found — `baked: 0`, and
> everything recompiles from GLSL on the main thread. It is silent and looks
> exactly like a baking bug. Note that building *before* committing bakes the
> pre-commit hash into the binary. Since Task 36 the runtime warns once when a
> shipped cache is present but nothing matches.

### What a container ships, and why the SPIR-V is usually not in it

Every shader container used to carry two descriptions of the same shader: the
SPIR-V glslang produced, and the WGSL the converter baked from that SPIR-V. Both
uncompressed — the WebGPU container is the one container format in the engine
that never compresses its code, because the web runtime pays CPU for
decompression in the one place where CPU is the scarce resource.

That matters because loading shaders is most of a web export's startup. `Servers:Rendering`
measures ~510 ms on *every* project, an empty scene included, of which
`createShaderModule` for all 363 modules is ~22 ms and pipeline creation is
0 ms. The rest is CPU spent moving shader bytes around: out of the `.pck`, into
a container, into a buffer the driver can hand to Dawn.

So a container now ships only what the runtime will read:

- **The baked WGSL is read as a window, not a copy.** `from_bytes()` keeps a
  reference to the buffer it parsed (`_from_bytes_begin()` on the base class)
  and the footer parse records `(offset, length)` per stage instead of
  allocating a `CharString` and copying the text into it. The driver has to copy
  the text anyway — its WGSL text passes mutate it in place — so the
  intermediate copy bought nothing.
- **The SPIR-V is dropped entirely when nothing can need it**
  (`FLAG_SPIRV_OMITTED`). The runtime reads a stage's SPIR-V for exactly four
  things, and three of them are now baked into the container instead: the
  pre-DCE image declarations (see `spirv_lite_reflect.h`), the "declares
  specialization constants" bit, and — moot once the WGSL is baked — the runtime
  conversion fallback. The fourth is the legacy specialization path, which re-patches the
  SPIR-V with constant values and re-converts it; a shader that can reach that
  path keeps its bytes. SPIR-V is the *larger* of the two payloads, so this is
  the larger saving, and it shrinks the `.pck` by the same amount.
- **`ShaderRD` frees each variant's bytecode once its group has loaded**
  (`API_TRAIT_RELEASE_SHADER_BYTECODE_AFTER_LOAD`). Nothing reads it after that,
  and holding ~120 MB of it for the session is a different proposition in a WASM
  heap than on a desktop.

`WEBGPU_BAKE_KEEP_SPIRV=1` on the exporting editor keeps the SPIR-V anyway. It
is how the saving was measured (two exports, one build) and an escape hatch if
a shader ever turns out to need bytes the rule thought it could not.

If the rule is ever wrong, the driver says so: a shader that reaches the legacy
specialization path with no SPIR-V to re-patch prints an error naming itself,
rather than quietly rendering with default specialization values.

Read `godotWebGPUShaderStats` in the browser devtools console to see which
tier each shader stage actually came from:

```js
godotWebGPUShaderStats
// { baked: 392, precompiled: 1, cached: 1, translated: 0, specialized: 0,
//   translatedShaders: [] }
```

`translated: 0` with an empty `translatedShaders` is the healthy state, and is
what a correctly baked export reaches — verified on a real project from a cold
start with browser storage cleared.

`translatedShaders` names the shaders behind `translated`, one entry per distinct
shader with an occurrence count (`"scene_forward_clustered x4"`), capped at 128
distinct names. A count tells you a gap exists; this tells you which shader, which
is what lets you fix it. The `--verbose` log carries the same thing, but a web
export has no convenient way to pass `--verbose`, so it is readable straight from
the console.

`translated` is the one that matters — it counts stages this driver converted
at load time *that baking should have covered*. Zero means every shader arrived
ready, and whatever startup cost remains is the browser compiling WGSL into
pipelines, which baking cannot remove. A non-zero value with baking enabled
points at a real gap; run with `--verbose` and the driver names each one as it
happens (the log line carries the owning shader's name).

Diagnosing such a gap is a matter of reading logs rather than guessing, since a
shader version is otherwise identified only by a SHA1 of its generated code.
Enable verbose in an exported build with the **`debug/settings/stdout/verbose_stdout`**
project setting (a web export has no command line), and these line up:

| Log line | Side | Says |
|---|---|---|
| `Shader baker: baking '<name>/<group>/<sha1>' from <origin>` | editor | which versions were baked, and what led the baker to each |
| `WebGPU shader baker: N stages -> U unique to convert (D duplicates, X unconvertible, C from the WGSL cache); ...` | editor | the batch's one-line summary: how much the converter actually had to do, and how many stages it rejected or the tool failed on |
| `Shader cache miss for <name>/<group>/<sha1>` | runtime | exactly which key the game wanted |
| `^ version is: uni=… frag=… defines(n)=[…]` | both | every field the version SHA1 covers, hashed separately, so the *differing* field names itself |
| `^ code[FRAGMENT]: …` | runtime | the generated shader body itself, when hashes are not enough |
| `BaseMaterial3D: generating shader for '<path>' … shading_mode=… disable_fog=…` | both | which **material** produced a generated shader — a generated shader has no path of its own, the material usually does |

`specialized` also converts at load time, but is **not** a baking gap and is
counted separately for that reason. It is a shader whose specialization
constants had to be patched into the SPIR-V because
`spirv_preprocess::spec_constants_overridable()` rejected it (non-scalar
constants, an `OpSpecConstantOp` the override path cannot lower, spec-constant array
sizes/composites/workgroup sizes — see "Specialization constants" above). The
patched bytes are built at pipeline-creation time from values the exporter never
saw, so no export-time bake could have produced them. The only way to reduce
this number is to widen what `spec_constants_overridable()` accepts, so that
more shaders specialize through WGSL `override` declarations on one base module
instead. A high `specialized` with `translated: 0` is a working, fully-baked
build.

**Two traps worth knowing**, both of which look exactly like "baking did
nothing":

- `bin/wgsl_convert_cli` is a **separate native build** from the editor
  (`drivers/webgpu/wgsl_cli/build.sh`), and the baker runs the copy sitting
  next to the editor executable. A stale copy bakes stale WGSL — and a copy
  predating WGSL `override` support bakes shaders whose specialization
  constants are frozen, which sends every specialized pipeline back down the
  legacy runtime-translation path. Rebuild it whenever anything under
  `drivers/webgpu/spirv_preprocess.*`, `spirv_cross_wrapper.*` or
  `thirdparty/spirv-cross-wgsl` changes. An editor built before the rename
  (2026-10-25) looks for `tint_convert_cli` and finds nothing: it warns once
  and ships every shader unbaked (TASKS.md §3 trap 61).
- Baked containers are cached between exports. After changing anything that
  affects WGSL output, clear `res://.godot/shader_cache` before re-exporting
  or the old bake is served back.

### Specialization Constants
Godot's specialization constants are always scalar (`bool`/`int`/`float`), which
is exactly what WGSL's `override` mechanism covers, so they are normally left in
the SPIR-V for SPIRV-Cross to turn into `@id(N) override` declarations and set with
WebGPU pipeline constants at `wgpuDeviceCreate*Pipeline()` time — one base shader
module serves every value combination, with no runtime SPIR-V patching or
re-conversion.

`spirv_preprocess::spec_constants_overridable()` decides this per module. A
module whose constants cannot all become overrides — a non-scalar one, an
`OpSpecConstantOp` operation the converter cannot lower, a specialization-constant-sized
array, an `OpSpecConstantComposite` built from one, a spec-constant workgroup
size — is frozen to its defaults by `freeze_spec_constant_ops()` instead, and
such a shader specializes through the legacy path:
`_create_module_with_spec_constants()` re-patches the original SPIR-V with the
real values and re-runs the whole pipeline, once per distinct combination, at
runtime. The choice is all-or-nothing per shader: if any stage that declares
specialization constants ends up frozen, the whole shader takes the legacy path,
since mixing the two would leave that stage silently on its defaults.

The rules were re-checked when SPIRV-Cross replaced Tint and stand unchanged:
of the 276 engine variants, 93 keep their spec constants as overrides (216
overrides) and none is frozen, so the legacy path serves user shaders only.
SPIRV-Cross lowers every scalar `OpSpecConstantOp` Tint could; it cannot convert
a spec composite used by runtime code or shuffled, nor a function-scope array
sized by a spec constant (both stay frozen). `SMod` follows WGSL's truncated
`%`. `freeze_spec_constant_ops()` evaluates `CompositeExtract`/`Insert`,
`VectorShuffle` and component-wise vector ops on constant composites, and
`SRem`/`SMod` with Vulkan's values; an op it cannot evaluate warns and becomes
`OpConstantNull`, never a silent 0. `wgsl_convert_cli --spec-info <file.spv>`
says which path a module takes; `webgpu_tests/spirv_cross_eval/run_spec_ops.sh`
(real GPU) probes every op kind on both paths.

### Modules and compute pipelines are created on first use

`shader_create_from_container()` keeps each stage's final WGSL and creates the `WGPUShaderModule` when a pipeline
first needs it (`_get_stage_module()`); `compute_pipeline_create()` records the request and compiles on the first
`command_bind_compute_pipeline()` (`_ensure_compute_pipeline()`). Chrome parses modules and compiles pipelines on the
GPU process's main thread, which also produces the page's frames, and the engine loads every variant of a shader and
creates every effect's compute pipelines at init. Eagerly, that was the startup stall (TASKS.md §2.4): ~90 compute
pipelines and ~500 modules before the first frame, none of the pipelines and ~5% of the modules actually used. Read a
stage's module through `_get_stage_module()`, never `stage_modules[]` directly.

From ~120 frames after startup, `_prewarm_compute_pipelines()` compiles the deferred compute pipelines in the
background with `createComputePipelineAsync()` (one per frame, two in flight), so an effect turned on later does not
hitch; a pipeline bound first compiles synchronously and the late result is dropped. `godotWebGPUPipelineStats` in
the page says how many were deferred, prewarmed and compiled on first use.

### Render pipelines compiled in the background are created asynchronously

`createRenderPipeline()` compiles on that same GPU-process thread, ~0.5 s for a Forward+ variant on a desktop GPU, so
every specialization the renderer asked for froze the game until it was done (a new surface or a decal coming into
view). `PipelineHashMapRD::compile_pipeline()` -- a material's ubershader compiled while its scene loads, and the
specializations the renderer draws with the ubershader until they are ready -- now turns on
`RD::set_pipeline_creation_async()` around the creation. While it is on, `render_pipeline_create()` goes through
`createRenderPipelineAsync()` (Chrome compiles that on a worker thread) and returns a pipeline with
`render_pending` set and a null handle; `_render_pipeline_async_cb()` fills it in. `PipelineHashMapRD` keeps it out of
its map until `pipeline_is_ready()`, exactly like a native background compile still running, so it is never bound
before then (`command_bind_render_pipeline()` refuses one that is). A caller that cannot wait -- `get_pipeline()` with
`p_wait_for_compilation`, or a pipeline still pending after 300 frames, which a render thread that never returns to
its event loop would cause -- gets it compiled synchronously instead and the late result is released; freeing a
pending pipeline does the same (`render_pipelines_in_flight`). Strip pipelines need a second, Uint16 variant and
stay synchronous. Shader modules cannot be created asynchronously in WebGPU, so a variant's modules are still created
on its first pipeline. Measured in TASKS.md §2.4.

### Barrier No-ops
WebGPU tracks resource hazards automatically. All barrier/sync commands are
no-ops.

### Buffer Mapping
WebGPU buffer mapping is asynchronous. Driver uses a **shadow buffer** pattern:
maintains a CPU-side copy, flushes to GPU via `wgpuQueueWriteBuffer()` on
unmap. Buffer reads use `wgpuBufferMapAsync` with callbacks.

### Bind Group Layout (BGL) Rebinding
When a shader's expected BGL doesn't match the uniform set's BGL (e.g., due to
specialization constant variants or merged push constant layouts), the driver
creates **adapted bind groups** on-the-fly using the shader's layout. A cache
prevents redundant re-creation.

## Known Limitations

### Differences from native Vulkan that remain (2026-10-17 parity sweep, TASKS.md §4.29)

Measured with `webgpu_tests/forward_plus/run_forward_plus.mjs --parity`. Every
item is a WebGPU spec limit or has a stated reason.

- **No image atomics in WGSL.** SDFGI voxelization ORs facing bits with a plain
  read-modify-write (concurrent fragments can drop bits; 0.03/255 in the
  matrix). FSR2 and volumetric fog keep their atomics on storage buffers.
- **FSR2 runs fp32** (no `shader-f16` path here) with half-float lookup tables
  (16-bit normalized formats are unfilterable on WebGPU): edges differ from
  native's fp16 path by ~0.55/255 mean.
- **MSAA is 1x or 4x** -- WebGPU's only counts. 2x becomes 4x, 8x becomes 4x.
- **No polygon mode**: `render_mode wireframe` and the wireframe debug draw
  render solid. **No VRS, no multiview** (XR).
- **No sampler border colour**: clamp-to-border reads the edge texel. The one
  Forward+ user (screen-space contact shadows) handles its border in the
  shader.
- **Sampler LOD bias** is applied to implicit-LOD samples through the scene
  shaders' render-pass samplers (all of Godot's biased ones); other samplers
  never carry a bias in Godot.
- **`alpha_to_one`** is ignored (alpha-to-coverage works); stencil compare and
  write masks are one per pipeline, not per face.
- **Texture arrays** beyond a stage's texture budget stay collapsed to element
  0 (a project with several LightmapGI or VoxelGI nodes and texture-heavy
  materials); the driver warns when that loses data.
- **HDR output** is not implemented (`SUPPORTS_HDR_OUTPUT` false).

- **Max 4 bind groups** (WebGPU spec) — Godot uses sets 0–3, with set 3 shared
  between material uniforms and push constant ring buffer.
- **No 3-component texture formats** — RGB8, RGB16F, RGB32F are unsupported as
  texture formats in WebGPU. The driver maps these to RGBA equivalents.
  Likewise **no texture component swizzle**, so L8/LA8 cannot broadcast to
  `(R,R,R,1)`/`(R,R,R,G)` at sample time and are expanded to RGBA8 in the data.
  Both expansions are unconditional on every WebGPU device, so they are logged
  as plain verbose notes (`Expanded RGB8 to RGBA8 (WebGPU has no 3-component
  texture formats)`), *not* as upstream's "not supported by hardware" warning —
  that wording describes a per-GPU shortfall and cost two separate
  investigations before it was changed. Monochrome font atlases sidestep the
  conversion entirely by rasterizing as RGBA8 up front (Task 35); the remaining
  conversions run once per texture at load, not per upload.
- **Multi-draw-indirect** — Uses the native `multi-draw-indirect` device feature
  when available and the indirect buffer's stride matches WebGPU's implicit
  tightly-packed draw-struct layout (16/20 bytes); falls back to dispatching
  each indirect draw individually otherwise (unsupported browser/GPU, or a
  non-standard stride).
- **No user-facing subgroup operations** — `LIMIT_SUBGROUP_IN_SHADERS` reports
  0, so custom `.gdshader`/visual-shader code cannot use subgroup intrinsics.
  This is narrower than it sounds: the `subgroups` device feature *is*
  requested, but the built-in engine shaders that use subgroup ops
  (`cluster_render.glsl`'s light culling, `scene_forward_clustered.glsl`'s
  cluster loop) have them lowered to single-lane equivalents by the driver's
  own SPIR-V preprocessing (`fold_ballot_bit_count`, `lower_subgroup_ops`),
  because Firefox's naga rejects `enable subgroups` and a baked export cannot
  know which browser will run it (TASKS.md Task 38). Both uses are
  optimizations over a correct per-lane computation, so nothing is lost but the
  optimization.
- **Forward+ (Clustered) renderer is supported** — the driver requests
  `maxSampledTexturesPerShaderStage` at the adapter's actual limit (not a
  conservative default), so Forward+'s ≥48-sampled-textures-per-stage
  requirement is met on WebGPU/Dawn and it is no longer auto-downgraded to
  Mobile. Both renderers work; Mobile remains this fork's most heavily
  live-tested path, but Forward+ (including SDFGI, SSR, SSAO/SSIL, FSR1/2)
  has had extensive real-project verification too — see
  `webgpu_notes/TASKS.md` Phase 9.
- **Timestamp queries** — Optional; depend on the `timestamp-query` device
  feature, which current Chrome and Firefox offer (Safari untested); without it
  they read 0. Each timestamp is the end-of-pass write of an empty compute
  pass; the beginning write goes to a spare query, because emdawnwebgpu cannot
  omit the index (`webgpu_notes/TASKS.md` §3 trap 105). Results come back
  through a ring of 4 `MAP_READ` buffers per frame slot, never waited for, so
  `viewport_get_measured_render_time_gpu()`, the Visual Profiler and dynamic
  resolution see values a few frames old on the web (§4.89).
- **Synchronous readback** — Not available in WebGPU. The first synchronous
  read of a texture or a buffer returns empty (a `null` Image, an empty
  array: `RenderingDevice::buffer_get_data()` no longer falls through to a
  stalling staging copy that could only return zeros, `webgpu_notes/TASKS.md`
  §4.96); later reads return the previous copy, and debug builds warn once per
  resource (§4.84, §4.92). Engine code that reads a GPU buffer synchronously
  must treat empty as "no data yet". RenderingDevice's asynchronous downloads
  (`texture_get_data_async`, `buffer_get_data_async`, and
  `Texture2D.get_image_async` / `get_image_awaitable` on top) wait for the
  staging buffer's `mapAsync` through `buffer_get_readback_status()`, so their
  callbacks always get the real data.
- **`threads=yes`** — all four `threads` × `dlink_enabled` combinations now
  boot. `threads=yes dlink_enabled=no` was fixed in Task 12 bug #1 (the
  `API_TRAIT_REQUIRES_SYNCHRONOUS_PIPELINE_COMPILATION` dispatch fix).
  `threads=yes dlink_enabled=yes` — GDExtension and threads together — was
  broken until 2026-10-07 by two bugs in Emscripten's own glue, and **now
  requires a patched toolchain**: run `misc/emsdk_patches/apply.sh --apply`
  (`build-linux.sh` does it for you). It is an initialization-order race
  in `libdylink.js` that stopped the export booting at all (`0001`): without it a
  `threads=yes` dlink build does not boot, and the build warns. `emsdk install` overwrites the toolchain tree, so
  it must be re-applied after every toolchain change — but scons does now
  relink on its own when the toolchain moves, so there is nothing to delete by
  hand. See `misc/emsdk_patches/README.md` and Task 12 bug #2 / Task 14
  subtask 1.5.

  Note that `threads=yes` is a *build* configuration, not a rendering change:
  WebGPU object handles live in a per-thread JS table (emdawnwebgpu's
  `WebGPU.Internals.jsObjects`), so shader-module and pipeline creation stay on
  the thread that imported the device no matter how many threads exist. Threads
  cannot move that work off the critical path.

  Threaded builds also require the page to be served cross-origin isolated
  (`Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp`), since they need
  `SharedArrayBuffer`.

  **Verified at the level stated and no further**: `threads=yes
  dlink_enabled=yes` has been confirmed to boot, complete startup and run its
  own game scripts with **zero console errors**, headless over COOP/COEP,
  against a real GDExtension + thread-support project (2026-10-07). It has not
  been run on real hardware, nor profiled, nor checked for stability beyond
  startup — and per the note above, threads cannot move shader/pipeline work off
  the critical path anyway, so "it boots" is not "it is worth enabling".

## Build Instructions

```bash
# Prerequisites: Emscripten 6.0.12 (this fork's pinned version) with emdawnwebgpu port
source /path/to/emsdk/emsdk_env.sh

# Build web template (debug, no threads, WebGPU only)
scons platform=web target=template_debug dlink_enabled=yes webgpu=yes opengl3=no threads=no -j$(nproc)

# Build web template (release)
scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j$(nproc)

# Build with both WebGPU and WebGL2 support
scons platform=web target=template_debug dlink_enabled=yes webgpu=yes opengl3=yes threads=no -j$(nproc)

# Build macOS editor (does not include WebGPU driver, for reference)
scons platform=macos target=editor -j$(nproc)
```

The build flag `webgpu=yes` enables `WEBGPU_ENABLED` and adds
`--use-port=emdawnwebgpu` to both compile and link flags.

## Project Settings

The rendering driver is selected via project settings:

- `rendering/renderer/rendering_method.web` — `forward_plus`, `mobile`, or
  `gl_compatibility` (default)
- `rendering/rendering_device/driver.web` — `webgpu` (used when rendering
  method is `forward_plus` or `mobile`)

When `gl_compatibility` is selected, the existing WebGL 2.0 / GLES3 path is
used instead.

## Browser Compatibility

| Platform | Browser | Status |
|----------|---------|--------|
| macOS | Chrome 113+ | 100% — all demos and benchmarks pass |
| macOS | Safari 18+ | 100% — all demos and benchmarks pass |
| macOS | Firefox | 100% — all demos and benchmarks pass |
| Android | Chrome | 99% — minor edge cases |
| iOS | Safari | Mostly — some limitations |

The HTML export shell automatically detects WebGPU availability and shows a
clear error message if the browser doesn't support it.

## Per-frame cost policy (2026-10-15, TASKS.md §2.17)

The web build is CPU-bound on the main thread, so what matters is the number of WebGPU calls and bytes per frame, not shader
work. The rules the driver now follows, each found by measurement and guarded by `webgpu_tests/perf_guard`:

- **One command encoder and one submit per frame** (plus rare forced splits). `command_begin_render_pass` no longer finishes
  and re-submits the encoder before passes whose attachment also has TextureBinding usage; that cost 80 encoders and submits
  a frame in a post-processing scene.
- **Uploads send only what was written.** `buffer_flush_range()` (base-class default = full flush) is used by dynamic persistent
  buffers: the scene instance buffer and the canvas instance buffer pass the bytes actually filled instead of the whole slice.
- **Staging-block writes are deferred and merged.** `command_copy_buffer()` records the union of the regions copied out of a
  staging block; `_queue_submit()` uploads each block once with one `wgpuQueueWriteBuffer` before every `wgpuQueueSubmit`. Never
  call `wgpuQueueSubmit` directly; go through `_queue_submit()`, or the copies read stale staging data.
- **Instance index via `firstInstance`** (`API_TRAIT_FIRST_INSTANCE_INDEX`): Forward Mobile and Forward+ send a single
  non-multimesh instance's index as drawIndexed's firstInstance and re-send the push constant only when another field changes.
- **Shader code can cost more on the GPU than the same GLSL does natively.** After the SPIR-V -> WGSL -> SPIR-V round trip,
  patterns a native driver folds may survive into Dawn's output: SSIL ran 12x native because a `const` sample-pattern array was
  re-initialized per tap (now hoisted by `spirv_preprocess::hoist_constant_local_arrays`). Compare per pass before assuming
  CPU cost: `gpu_time.mjs --passes` against the native project's `--gpu-profile`.
- **Read-write storage textures bind as `read_write` when the format allows it** (`_wgsl_format_supports_read_write()`: WGSL
  `readonly_and_readwrite_storage_textures` plus r32, or the tier2 set). Only other formats are split into write + shadow read,
  and every split costs a full-texture copy before each dispatch that binds it (`rwShadowRefreshes` in the frame stats).
- Profiling tools: `webgpu_tests/perf_guard/api_profile.mjs` (busy %, calls/ms per frame per GPU method), `cpu_steady.mjs`
  (wasm CPU by function, needs `debug_symbols=yes`) and `gpu_time.mjs` (GPU ms per frame from timestamp queries; `--passes`
  ranks every render/compute pass, `--replace-wgsl` swaps in a hand-edited shader module to bisect one).
