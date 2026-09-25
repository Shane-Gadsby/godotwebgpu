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
│    • SPIR-V → WGSL translation (Tint, linked in)        │
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
| `rendering_shader_container_webgpu.cpp/h` | ~210 | Shader container format (SPIR-V storage + Tint WGSL conversion) |
| `webgpu_objects.h` | ~320 | GPU object wrappers (WGBuffer, WGTexture, WGShader, etc.) |
| `spirv_preprocess.cpp/h` | ~1700 | SPIR-V preprocessing passes before Tint conversion |
| `tint_wrapper.cpp/h` | ~55 | C++20 isolation wrapper for Tint API |
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
GLSL → SPIR-V (glslang, at build time) → WGSL (Tint, linked in as C++).
SPIR-V is preprocessed in C++ before Tint conversion: combined image-samplers
are split into separate texture + sampler bindings, push constant blocks are
rewritten to storage buffer references at binding 120, and various other
fixups (depth image flags, position Y negation, point size stripping) are
applied. Tint is compiled as a thirdparty C++20 library via a thin wrapper
(`tint_wrapper.cpp`) that isolates its C++20 headers from the Godot build.

### Shader Precompilation
Three tiers, checked in order:

1. **Export-time bake** — with `shader_baker/enabled` on (the default for Web
   presets), `ShaderBakerExportPlugin` walks every `ShaderRD` the engine
   embeds *and* every material shader reachable from the exported resources,
   and stores each stage's WGSL inside that stage's own shader container in
   the `.pck`. The runtime reads it back by object identity, so there is no
   hash lookup to drift. Since specialization constants became WGSL overrides
   (below), one baked base module covers every value combination.
2. **Build-time table** — `wgsl_precompile.py` bakes the engine's own
   ubershaders into `wgsl_precompiled.gen.h` during `scons ... webgpu=yes`.
   Keyed by SPIR-V hash, so it only hits when the engine's glslang output
   matches what the table was generated from.
3. **Runtime Tint** — anything that missed, translated in the browser on
   demand. This is what an export with baking turned off uses for everything,
   and it is the only route for a shader that does not exist at export time
   (`Shader.new()` + `set_code()` at runtime).

Read `godotWebGPUShaderStats` in the browser devtools console to see which
tier each shader stage actually came from:

```js
godotWebGPUShaderStats
// { baked: 412, precompiled: 3, cached: 88, translated: 0, specialized: 37,
//   translatedShaders: [] }
```

`translatedShaders` names the shaders behind `translated`, one entry per distinct
shader with an occurrence count (`"scene_forward_clustered x4"`), capped at 128
distinct names. A count tells you a gap exists; this tells you which shader, which
is what lets you fix it. The `--verbose` log carries the same thing, but a web
export has no convenient way to pass `--verbose`, so it is readable straight from
the console.

`translated` is the one that matters — it counts stages this driver ran Tint on
at load time *that baking should have covered*. Zero means every shader arrived
ready, and whatever startup cost remains is the browser compiling WGSL into
pipelines, which baking cannot remove. A non-zero value with baking enabled
points at a real gap; run with `--verbose` and the driver names each one as it
happens (the log line carries the owning shader's name).

`specialized` also runs Tint at load time, but is **not** a baking gap and is
counted separately for that reason. It is a shader whose specialization
constants had to be patched into the SPIR-V because
`spirv_preprocess::spec_constants_overridable()` rejected it (non-scalar
constants, an `OpSpecConstantOp` Tint cannot lower, spec-constant array
sizes/composites/workgroup sizes — see "Specialization constants" above). The
patched bytes are built at pipeline-creation time from values the exporter never
saw, so no export-time bake could have produced them. The only way to reduce
this number is to widen what `spec_constants_overridable()` accepts, so that
more shaders specialize through WGSL `override` declarations on one base module
instead. A high `specialized` with `translated: 0` is a working, fully-baked
build.

**Two traps worth knowing**, both of which look exactly like "baking did
nothing":

- `bin/tint_convert_cli` is a **separate native build** from the editor
  (`drivers/webgpu/tint_cli/build.sh`), and the baker runs the copy sitting
  next to the editor executable. A stale copy bakes stale WGSL — and a copy
  predating WGSL `override` support bakes shaders whose specialization
  constants are frozen, which sends every specialized pipeline back down the
  legacy runtime-translation path. Rebuild it whenever anything under
  `drivers/webgpu/spirv_preprocess.*`, `tint_wrapper.*` or `thirdparty/tint`
  changes.
- Baked containers are cached between exports. After changing anything that
  affects WGSL output, clear `res://.godot/shader_cache` before re-exporting
  or the old bake is served back.

### Specialization Constants
Godot's specialization constants are always scalar (`bool`/`int`/`float`), which
is exactly what WGSL's `override` mechanism covers, so they are normally left in
the SPIR-V for Tint to turn into `@id(N) override` declarations and set with
WebGPU pipeline constants at `wgpuDeviceCreate*Pipeline()` time — one base shader
module serves every value combination, with no runtime SPIR-V patching or Tint
conversion.

`spirv_preprocess::spec_constants_overridable()` decides this per module. A
module whose constants cannot all become overrides — a non-scalar one, an
`OpSpecConstantOp` operation Tint cannot lower, a specialization-constant-sized
array, an `OpSpecConstantComposite` built from one, a spec-constant workgroup
size — is frozen to its defaults by `freeze_spec_constant_ops()` instead, and
such a shader specializes through the legacy path:
`_create_module_with_spec_constants()` re-patches the original SPIR-V with the
real values and re-runs the whole pipeline, once per distinct combination, at
runtime. The choice is all-or-nothing per shader: if any stage that declares
specialization constants ends up frozen, the whole shader takes the legacy path,
since mixing the two would leave that stage silently on its defaults.

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

- **Max 4 bind groups** (WebGPU spec) — Godot uses sets 0–3, with set 3 shared
  between material uniforms and push constant ring buffer.
- **No 3-component texture formats** — RGB8, RGB16F, RGB32F are unsupported as
  texture formats in WebGPU. The driver maps these to RGBA equivalents.
- **Multi-draw-indirect** — Uses the native `multi-draw-indirect` device feature
  when available and the indirect buffer's stride matches WebGPU's implicit
  tightly-packed draw-struct layout (16/20 bytes); falls back to dispatching
  each indirect draw individually otherwise (unsupported browser/GPU, or a
  non-standard stride).
- **No user-facing subgroup operations** — `LIMIT_SUBGROUP_IN_SHADERS` reports
  0, so custom `.gdshader`/visual-shader code cannot use subgroup intrinsics.
  This is narrower than it sounds: the `subgroups` device feature *is*
  requested and used internally — the driver's own SPIR-V preprocessing
  (`tint_wrapper.cpp`'s `allow_non_uniform_subgroup_operations`) and
  build-time precompilation (`wgsl_precompile.py`'s SPIR-V 1.3 target) both
  support subgroup ops in built-in engine shaders (e.g. `cluster_render.glsl`,
  used by the Forward+/Clustered renderer's light culling) — it's only the
  RenderingDevice-facing capability query for user shaders that's hardcoded
  off.
- **Forward+ (Clustered) renderer is supported** — the driver requests
  `maxSampledTexturesPerShaderStage` at the adapter's actual limit (not a
  conservative default), so Forward+'s ≥48-sampled-textures-per-stage
  requirement is met on WebGPU/Dawn and it is no longer auto-downgraded to
  Mobile. Both renderers work; Mobile remains this fork's most heavily
  live-tested path, but Forward+ (including SDFGI, SSR, SSAO/SSIL, FSR1/2)
  has had extensive real-project verification too — see
  `webgpu_notes/TASKS.md` Phase 9.
- **Timestamp queries** — Optional; depend on the `timestamp-query` device
  feature. Graceful fallback to dummy results when unavailable.
- **Synchronous readback** — Not available in WebGPU. Timestamp and buffer
  readbacks use async callbacks with shadow buffers.
- **`threads=yes`** — supported with `dlink_enabled=no` (the common case;
  fixed and live-verified — see `webgpu_notes/TASKS.md` Task 12). The
  `dlink_enabled=yes threads=yes` combination (GDExtension support together
  with threads) is a known-unsupported configuration: it hits a genuine
  initialization-order race inside Emscripten's own dylink+pthread runtime
  glue (`libdylink.js`), not this fork's code, and is not planned to be
  patched around here — see Task 12 for the full root-cause trail. Reconfirmed
  2026-09-19 against a real-project export: `threads=no`/`dlink_enabled=no`,
  `threads=no`/`dlink_enabled=yes`, and `threads=yes`/`dlink_enabled=no` all
  work; `threads=yes`/`dlink_enabled=yes` fails with the same signature Task
  12 already root-caused — no regression, no new information.

## Build Instructions

```bash
# Prerequisites: Emscripten 6.0.9 (this fork's pinned version) with emdawnwebgpu port
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
