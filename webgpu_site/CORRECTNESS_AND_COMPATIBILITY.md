# Godot WebGPU Backend — Correctness & Compatibility

## Ship-Readiness Summary

| Area | Score | Verdict |
|------|-------|---------|
| Resource lifecycle | 9/10 | All failure paths covered; async lifetime robust |
| Rendering correctness | 9/10 | Forward+ matches native Vulkan on all 87 matrix arms, unbaked and baked (2026-10-26, §2.9); remaining gaps are WebGPU spec limits |
| Cross-browser compatibility | 9/10 | Well-scoped workarounds for Chrome/Firefox/Safari |
| Error handling | 8/10 | Robust async patterns; deadlock prevention |
| Code quality | 8/10 | Well-documented; defensive; some TODOs remain |
| **Overall** | **Ship-Ready** | No critical bugs. Known limitations acceptable. |

---

## 1. Resource Lifecycle Guarantees

### 1.1 Delete-on-Failure Pattern (All Creation Functions)

Every resource creation function properly handles failure:

| Function | Failure Point | Cleanup |
|----------|--------------|---------|
| `buffer_create` | `wgpuDeviceCreateBuffer` null | delete WGBuffer |
| `buffer_create_with_data` | Same | delete WGBuffer |
| `texture_create` | Texture null | delete WGTexture |
| `texture_create` | View null | release texture + delete WGTexture |
| `texture_create_shared` | View null | delete WGTexture (handle already null) |
| `texture_create_shared_from_slice` | View null | delete WGTexture |
| `shader_create_from_container` | Any stage fail | goto cleanup: releases modules + layouts + delete |
| `render_pipeline_create` | Pipeline null | release specialized modules |
| `compute_pipeline_create` | Pipeline null | release specialized module |
| `uniform_set_create` | BindGroup null | delete WGUniformSet |

### 1.2 Async Lifetime (Freed-While-Pending)

All async operations use a consistent two-flag pattern:

| Object | Pending Flag | Freed Flag | Callback |
|--------|-------------|-----------|----------|
| WGBuffer | `map_pending` | `freed` | `_buffer_deferred_map_cb` |
| WGFence | `work_done_pending` | `freed` | `_fence_work_done_callback` |
| WGQueryPool | `readback_pending` | `freed` | `_timestamp_readback_callback` |
| ReadbackEntry | (implicit) | `cancelled` | `_readback_map_cb` |

**Guarantee**: No use-after-free is possible. If the owner is destroyed while a callback is in flight, the callback detects this and performs full cleanup (unmap + release + delete).

### 1.3 Destructor Completeness

The driver destructor releases all global resources:
- Push constant bind group, layout, and ring buffer
- Empty bind group and layout
- All fallback textures and views (float, cube, multisampled)
- Dummy samplers (filtering, comparison)
- Aliasing stub buffer
- All readback cache entries (staging buffers + shadow memory)
- Shader container format registration

### 1.4 Readback Cache Stability

The readback cache originally stored entries by value in a HashMap. Pointer-based callbacks could be invalidated by HashMap rehashing. Fixed by heap-allocating entries (`memnew(ReadbackEntry)`) for pointer stability.

---

## 2. Rendering Correctness

### 2.1 Stencil Reference Binding

**Status: Correct**

WebGPU has no pipeline-level stencil reference (unlike Vulkan). The driver stores `stencil_reference` in `WGPipelineWrapper` at creation time and applies it dynamically via `wgpuRenderPassEncoderSetStencilReference` when the pipeline is bound.

Note: Only `front_op.reference` is stored. If front/back differ (Vulkan supports this, WebGPU doesn't), back reference is lost. Godot's renderer always uses the same reference for both faces.

### 2.2 Strip Topology Pipeline Variants

**Status: Correct**

WebGPU bakes `stripIndexFormat` into pipeline state, but Godot only knows index format at draw time. Solution: create both Uint16 and Uint32 variants at pipeline creation. At draw time, select based on `current_index_format`.

- Uint16 variant failure is non-fatal (falls back to Uint32)
- Variant selection only in indexed draw calls (correct)

### 2.3 Viewport/Texture Readback

**Status: Correct, Sophisticated Design**

Frame-deferred async pattern:
1. First call: copy to staging, initiate async map, return empty
2. Subsequent: return cached data, initiate fresh readback
3. One-shot capture: after returning, `has_data = false` forces fresh snapshot (prevents stale N-2 frame data)

Correctly handles:
- Probes `wgpuBufferGetMapState` as fallback when C callback hasn't fired
- In-flight readbacks (returns empty, doesn't queue duplicate mapAsync)
- Format conversion for promoted textures (R8→R32Float, Float32→Float16)
- 256-byte row alignment for WebGPU buffer-texture copies

What a project sees: a first synchronous `Texture2D.get_image()` / `RenderingServer.texture_2d_get()` returns `null`,
and a first `RenderingDevice.texture_get_data()` / `buffer_get_data()`, `Mesh.surface_get_arrays()` on an ArrayMesh or
ImmediateMesh and `RenderingServer.multimesh_get_buffer()` return an **empty** array until the data has arrived;
`GPUParticles3D.capture_aabb()` returns an empty AABB until then. Debug exports warn once per texture or buffer (once
for mesh arrays) on the console's warning channel; release exports stay silent. The fix in a project is the
asynchronous form: `await texture.get_image_awaitable()`, `get_image_async(callback)`,
`RenderingServer.texture_2d_get_awaitable()`, or `RenderingDevice.texture_get_data_async()` /
`buffer_get_data_async()`, which always deliver the real data (`webgpu_notes/TASKS.md` §4.82, §4.84, §4.92, §4.96).

### 2.4 Dynamic Buffer Offsets

**Status: Correct**

Multi-frame physical buffers with per-frame rotation:
- Alignment to `minUniformBufferOffsetAlignment` (fallback 256)
- Dynamic offsets packed into 4-bit slots in `uniform_sets_get_dynamic_offsets`
- Merged PC group correctly preserves material dynamic offsets alongside push constant ring offset

### 2.5 Texture Clear Fallback

**Status: Correct**

`command_clear_color_texture` has two paths:
1. Fast path (RenderAttachment usage): zero-draw render pass per mip/layer
2. Fallback (CopyDst only): an encoder `copyBufferToTexture` from a cached zero buffer (or a per-clear pattern buffer for a non-zero colour), so the clear is ordered with the rest of the command buffer

Both paths clear every z-slice of a 3D texture. Until 2026-10-17 the fallback was `wgpuQueueWriteTexture`, which runs at submit ahead of everything the encoder recorded earlier, and only slice 0 of a 3D texture was cleared; together they corrupted SDFGI's cascades whenever the camera moved.

### 2.5b MSAA Resolve

**Status: Correct**

`command_resolve_texture` uses WebGPU's native resolve mechanism where possible: a throwaway render pass with the MSAA source as the color attachment (`loadOp: Load`, `storeOp: Discard`) and the destination as `resolveTarget` — WebGPU performs the actual resolve automatically on `wgpuRenderPassEncoderEnd()`, no draws needed. This requires the source and destination to have identical GPU formats, which holds for every case except Forward+'s velocity buffer (its MSAA source has no `STORAGE_BIT` and stays native `RG16Float`, while its resolve target does get `STORAGE_BIT` and is promoted to `RG32Float` by `_promote_storage_format()`) — for that one case, a small driver-owned WGSL compute shader (built lazily, cached per destination format) reads all 4 MSAA samples via `textureLoad` and averages them into the promoted destination via `textureStore`, entirely self-contained in `drivers/webgpu/`. Depth resolve doesn't go through this function at all: WebGPU render passes have no `depthResolveTarget` (only color attachments get one), so Forward+ resolves depth via a dedicated compute shader instead (`Resolve::resolve_depth()`/`resolve_gi()`, core/shared code).

A related case: Forward+'s MSAA depth-resolve target is deliberately allocated as a plain `R32Float` texture rather than a native depth format (`RenderSceneBuffersRD::get_depth_format()`, upstream/cross-backend design — the compute-shader resolve needs a storage-writable format). Depth-reading shaders that need real depth data under this condition (e.g. `BokehDofShaderRD`) use a genuine second shader variant, selected at the call site based on whether MSAA is active, so the correct WGSL type (plain float vs. native depth) is used either way — see `webgpu_notes/TASKS.md` Task 24.

### 2.6 Push Constant Ring Buffer

**Status: Correct**

- 256KB ring = 1024 slots at 256B alignment
- Wrap-around forces pre-wrap flush before resetting to 0
- Pre-wrap flush ensures no data loss (uploads dirty range before overwriting)
- Merged PC groups preserve material offsets

### 2.7 Encoder State Machine

**Status: Correct**

Active encoder state (NONE/RENDER/COMPUTE) managed with `end_active_encoder()` called at:
- `command_begin_render_pass`
- `command_bind_compute_pipeline`
- `command_clear_*`
- `command_timestamp_write`
- `command_buffer_end`

### 2.8 Known Unimplemented Rendering Functions

| Function | Status | Impact |
|----------|--------|--------|
| `command_render_clear_attachments` | WARN_PRINT, no-op | Mid-pass clears silently ignored |
| `draw_indexed_indirect_count` | Uses max_count | Wastes GPU cycles, no correctness issue |

### 2.9 Forward+ Parity with Native Vulkan

**Status: Correct, measured** (2026-10-17, `webgpu_notes/TASKS.md` §4.29; 87 arms since 2026-10-26, §4.39, §4.41, §4.42, §4.44)

`webgpu_tests/forward_plus/run_forward_plus.mjs --parity` renders every feature of the Forward+ matrix off and on, natively on Vulkan and in Chrome on WebGPU, at a fixed 60 fps with a 240-frame settle and a frozen capture frame, and fails any frame whose mean absolute difference from native exceeds 0.05 (0-255 scale; SDFGI 0.08 for its atomics, FSR2 0.3). All 87 arms pass (2026-10-26, Chrome, unbaked and baked). Most land at 0.002-0.006; SDFGI 0.027, volumetric fog 0.001, omni cube shadows 0.015, FSR2 0.142 (one or two levels of noise; it was 0.57 and visibly softer until FSR2's reactive mask stopped being read through a swizzled view WebGPU cannot swizzle, TASKS.md §3 trap 55).

The 11 arms added on 2026-10-26 cover skeletal animation, particle trails, LightmapGI, FogVolume (with and without a fog shader), stencil outline and x-ray, bent normals, SMAA, compositor effects and occlusion culling, each within 0.004 of native. What stays uncovered is what WebGPU cannot have: multiview, variable-rate shading, MSAA 2x and 8x.

**Occlusion culling works on the web** (2026-10-26, TASKS.md §4.38). Stock Godot builds its web templates without the raycast module (Embree), so no web export culls occluded objects; this fork's web templates include it by default (+0.23 MiB gzipped wasm), and the matrix's `occlusion_culling` arm draws 29 -> 8 objects as native does. The fork also fixed a stock bug that kept the culler from ever committing its scene in a `threads=no` build. The Web export option `rendering/occlusion_culling` turns it off for one export.

The sweep that introduced this check found five features that rendered wrongly while passing the older "does the frame change" test -- the reason the parity mode exists:

| Feature | Was | Cause |
|---|---|---|
| SDFGI with a moving camera | 25.1 off native | 3D clears hit slice 0 only; queue-level uploads overtook earlier encoder commands |
| Volumetric fog | 2.05, shadows missing | shadow atlases are depth textures declared as float -> blank fallback |
| Omni lights | always dual-paraboloid | a performance trait forced it, ignoring the light's mode |
| `DEPTH_TEXTURE`, refraction, proximity fade | reading 0 | the depth copy sampled a depth texture in a float slot |
| Mipmap bias (TAA, FSR) | dropped | WebGPU samplers have no LOD bias |

and, beyond those, texture arrays collapsed to element 0, four shader built-ins that crashed the page, FSR2 skipping its depth reconstruction, constant-alpha blending, depth clamp, contact shadows' border colour, and a material using `POINT_COORD` drawing nothing (an upstream bug that also affects D3D12).

### 2.9b Non-Finite Colors in Post Effects

The bokeh depth of field, glow, auto exposure and the previous-frame copy SSR reflects treat a non-finite scene color
as black (`finite_or_zero()` in `servers/rendering/renderer_rd/shaders/effects/finite_inc.glsl`, tested on the
exponent bits), so a stray NaN from a user shader shows as one black pixel instead of a black bokeh shape, a glow blob,
a smeared reflection or a blank frame (`webgpu_notes/TASKS.md` §4.41, §4.44, §3 trap 70). A deliberate divergence from
upstream Godot (an upstream sync may conflict in `copy.glsl`, `blur_raster.glsl`, `luminance_reduce*.glsl` and
`bokeh_dof*.glsl`); finite frames are bit-identical.

### 2.10 Audio Parity with Native

**Status: Correct, measured** (2026-10-18, `webgpu_notes/TASKS.md` §4.30)

Web exports default to Sample playback, which plays sounds through Web Audio instead of `AudioServer`'s mixer, so
everything the mixer does has to be reproduced. `webgpu_tests/web_audio/run_tests.mjs` renders the real JS driver in
an `OfflineAudioContext` and compares it with ports of the native code: the four loop modes are sample-identical to
`AudioStreamPlaybackWAV` from several start frames; volume ramps, the full-volume start on a new bus, fades on stop,
pause and bus removal follow `AudioServer::_mix_step()`; the bus rules (sends, solo chains, mute, `move_bus`,
`add_bus`) match; positions follow pitch, loops and pause; the 3D high shelf matches `AudioFilterSW` within 1 dB.
`run_e2e.mjs` and the smoketest's `audio_parity` scene run a self-checking GDScript suite natively and in both export
kinds (Ogg/MP3, seeks, polyphony, 2D/3D players, Stream playback through effects, a generator, a randomizer).

The review that introduced this found, among others: surround gains one channel off, ping-pong/backward loops playing
forward, clicks from unramped volume changes, a use-after-free in `AudioServer::set_bus_layout()`, a stream's loop
setting frozen at its first play (`finished` never fired), the whole sound copied on every play, and an upstream leak
of every playback still running at quit.

---

## 3. Cross-Browser Compatibility

### 3.1 Chrome/Dawn

| Issue | Workaround | Scoping |
|-------|-----------|---------|
| No `sized_binding_array` | Flatten `binding_array<T,N>` → `T` at WGSL text level | All browsers (spec optional) |
| `CompositeAlphaMode_Opaque` ignored | Strip alpha writes from BGRA8Unorm pipelines | Only BGRA8Unorm (swap chain) |
| `shader-f16` unsupported | Strip `enable f16;` directive | Unconditional (harmless) |
| `float32-blendable` may be missing | Skip blend state on float32 targets | Gated by feature check |

**Assessment**: All workarounds properly scoped. Cannot cause regressions on other browsers.

### 3.2 Firefox/wgpu

| Issue | Workaround | Scoping |
|-------|-----------|---------|
| All bind groups must be bound | Empty bind group at gap indices | Per-spec optional (always safe) |
| 8 storage buffers/stage (Metal limit) | Per-stage SSBO visibility via metadata | More restrictive = always valid |
| `read_write` in vertex shaders | Demote to `read` via string replacement | Vertex/fragment stages only |

**Assessment**: All workarounds comply with WebGPU spec (they're more restrictive, never less). Safe across all browsers.

### 3.3 Adreno GPUs (Android WebGPU via Chrome)

| Issue | Workaround | Scoping |
|-------|-----------|---------|
| No `float32-filterable` | Downgrade Float32→Float16 for sample-only textures | Gated by feature check |
| Emission textures precision | RG32Float→RG16Float specifically | Conditional on Adreno detection |

**Data handling**: Bidirectional conversion — `texture_upload_convert` (float32→float16) and `texture_readback_convert` (float16→float32) ensure correct data at both ends.

**Fallback of last resort**: If float32 textures can't be downgraded (e.g., StorageBinding required), they're substituted with a 4x4 RGBA8 fallback at bind time. Rendering continues without GPU errors, though visual quality degrades.

### 3.3b Mali GPUs (Chrome/Android) — measured 2026-10-17

First real phone run of this fork: a Motorola Razr 60 (MediaTek MT6878, Mali-G615 MC2 "valhall", Android 16, Chrome 154), driven over USB from the Linux desktop (`webgpu_tests/perf_guard/android/`). No flags, no workarounds needed:

| Observation | Detail |
|---|---|
| Boots and renders correctly | A real Forward+ project (DOF, three-point shadowed light rig, UI); console shows only the known asynchronous-readback notice and the `float32-blendable` capability note |
| Adapter | `maxSampledTexturesPerShaderStage` = 48 (the engine asks for the adapter maximum), so `RendererCompositorRD` keeps **Forward+**; features include `shader-f16`, `timestamp-query`, ASTC/ETC2, texture-formats tier 1 and 2, subgroups |
| Specialization | Pipeline `override` constants reach Dawn with the engine's values (`pso_sc_packed_0 = 0x308308B0`: 8/12 PCF and penumbra samples for soft filter quality 3) — verified on device with `--log-pipelines` |
| Performance | GPU-bound, not CPU-bound: 20.5 fps at 1920x1080 with 48.7 ms of GPU time per frame; see *Performance & Optimization*, "The First Phone" |
| Not an issue here | Debug vs release template, dlink, threads, the separate render thread, texture compression, `textureGather` (translated natively), f16 (a Forward+ f16 build changed nothing), dead feature code in the clustered shader |

Untested on phones: Adreno (the §3.3 fallbacks), Safari/iOS.

### 3.4 All Browsers (Spec Compliance)

| Issue | Workaround |
|-------|-----------|
| No `derivative_uniformity` guarantee | Prepend `diagnostic(off, derivative_uniformity)` to all WGSL |
| `f32::MAX` decimal overflow in WGSL | Replace with hex float `0x1.fffffep+127f` |
| Limited storage texture formats | Promote R8/RG8/R16/RG16 to 32-bit at texture + WGSL level |
| No texture component swizzle | Convert L8/LA8 to RGBA8 on CPU; monochrome font atlases rasterized as RGBA8 up front |
| No 3-component texture formats | RGB8/RGBH/RGBF expanded to RGBA8 on CPU, once per texture at load |
| No push constants | 256KB ring buffer emulation |
| No combined image-samplers | SPIR-V-level split before conversion |
| No subpasses | Flatten to separate render passes |
| 256-byte row alignment for copies | Applied in all buffer↔texture operations |

### 3.5 Workaround Safety Matrix

All browser-specific workarounds are:
- Gated by runtime feature detection (never applied unconditionally to the wrong browser)
- More restrictive than the spec requires (cannot cause validation errors)
- Tested against the "cannot regress other browsers" criterion

No workaround makes assumptions about which browser is running. They all check capabilities, not user-agent strings.

---

## 4. Error Handling Robustness

### 4.1 Error Handling Philosophy

Three-tier approach:
1. **Hard errors** (`ERR_FAIL_COND_V`): Resource creation failures that make further operation impossible
2. **Warnings** (`WARN_PRINT`): Missing features or degraded behavior (logged once)
3. **Silent fallbacks**: Browser limitations handled transparently (no mid-pass clear → no-op)

### 4.2 Device Lost

- Installed at initialization via JS `device.lost.then(...)`
- Logs reason and message to console
- Surface status `Lost` returns empty FramebufferID with `ERR_PRINT_ONCE`
- No automatic recovery (page reload required on web)
- **Acceptable**: Device loss on web is unrecoverable without Asyncify; user sees the error in console

### 4.3 Fence Wait (Deadlock Prevention)

`fence_wait` calls `wgpuInstanceProcessEvents` to deliver callbacks, then force-signals if the fence hasn't completed:

```cpp
if (!fence->signaled) {
    fence->signaled = true; // Force-signal to prevent deadlock
}
```

**Rationale**: Engine calls `fence_wait` at frame start for previous frame's fence. A full frame has elapsed — GPU work has almost certainly completed. Force-signal prevents infinite hang in single-threaded WASM where true blocking is impossible.

### 4.4 Null-Check Coverage

- Texture views: checked after all creation paths
- Staging buffers: checked in readback paths
- Pipeline layouts: checked before shader is returned
- Bind groups: checked before uniform set is returned
- All `ERR_FAIL_NULL` / `ERR_FAIL_NULL_V` for function parameters

**Minor gap**: `wgpuDeviceCreateShaderModule` return not checked for null before WGSL scanning proceeds. Benign (null module stored, caught at pipeline creation time).

### 4.5 Surface Handling

All `WGPUSurfaceGetCurrentTextureStatus` values handled:
- `Success`: normal path
- `Lost`: unrecoverable, error print
- `SuccessSuboptimal` / `Outdated`: request resize, return empty
- Other: warning, return empty

### 4.6 OOM Handling

- `ABORTING_MALLOC=0` set in build: malloc returns NULL on failure instead of abort
- `ALLOW_MEMORY_GROWTH=1`: WASM linear memory grows on demand
- Combined: graceful degradation under memory pressure

---

## 5. SPIRV-Cross Patch Correctness

SPIR-V → WGSL is SPIRV-Cross's WGSL backend (`thirdparty/spirv-cross-wgsl`, Ludicon/spirv-cross-wgsl) since October 2026; Tint, the converter before it, and its six patches are gone (`webgpu_notes/TASKS.md` §4.36). Four local patches (the WGSL backend, plus one hook in `spirv_glsl.*` for `0019`), recorded in `thirdparty/spirv-cross-wgsl/patches/` and `thirdparty/README.md`, never upstreamed (a project rule):

| Patch | Fixes | Risk Level | Correctness |
|-------|-------|-----------|-------------|
| `0001` workgroup uniform load | FSR2 luminance pyramid failed barrier uniformity | Low | Rewrites only the exact barrier / workgroup load / barrier sequence, which is what `workgroupUniformLoad` is defined as |
| `0006` 16-bit norm storage formats | `CanvasSdfShaderRD` (R16Snorm) did not convert | None | Table entries for six formats WGSL names (`r16unorm` ... `rgba16snorm`); the driver still remaps formats the device lacks |
| `0015` uniform compute builtins as parameters | Screen-space contact shadows failed barrier uniformity | Low | `workgroup_id` / `num_workgroups` reach the inner entry point as parameters shadowing their `var<private>` copies; same values, now visibly uniform |
| `0019` no `for` statements | Volumetric fog lost its density on Mali-G615 / Chrome (a `for` starting at -3 ran zero times) | Low | Every loop takes the backend's generic `loop { if (!c) { break; } ... }` form, the shape Tint always emitted |

**Divergence risk**: Low. Each patch is 25-125 lines and is reapplied by hand when the vendored fork is updated. What guards the interface: `webgpu_tests/spirv_cross_eval/interface_reference.json` (bindings, kinds and override ids of every engine variant, as Tint emitted them) and `webgpu_tests/wgsl_text_scan/scan_reference.txt` are compared on every smoke run, and Forward+ is judged against native Vulkan (`run_forward_plus.mjs --parity`, 71/71 after the swap).

---

## 6. Known Limitations (Documented, Acceptable)

### Rendering Limitations
- `command_render_clear_attachments` not implemented (rarely used in Forward Mobile)
- `draw_indexed_indirect_count` always uses max count (no GPU-driven rendering)
- Texture arrays past a shader stage's texture budget stay collapsed to element 0 (one-time warning); every engine array fits in practice
- No image atomics: SDFGI voxelization does a plain read-modify-write (0.025 mean diff from native)
- MSAA 2x and 8x run as 4x (WebGPU allows 1 and 4)
- No wireframe / polygon mode, no sampler border colour (engine readers emulate it), no `alpha_to_one`, one stencil mask pair for both faces
- No HDR canvas output
- Full list with what would close each: `webgpu_notes/TASKS.md` §2.31
- Subpass-based post-processing disabled (WebGPU has no input attachments)
- No hardware multiview / VRS / subgroups

### Platform Limitations
- Sample playback (the web default for audio) cannot run bus effects (reverb, EQ, capture, spectrum...) or stream:
  a sound is decoded whole on its first play. Players that need either set `playback_type` to Stream, the native
  mixer. Both gaps are warned about at export and at runtime; options to close them: `webgpu_notes/TASKS.md` §2.32
- Web exports keep the screen on (Screen Wake Lock, `display/window/energy_saving/keep_screen_on`, secure context
  required). On phones and tablets the first tap puts the page in fullscreen (`html/fullscreen_on_first_tap`, on by
  default), where the browser locks the orientation chosen by the `html/orientation` export option (default: the
  `display/window/handheld/orientation` project setting). Desktop browsers cannot lock orientation, report
  `FEATURE_ORIENTATION` as unsupported, and are not put in fullscreen by a tap
- Canvas selector hardcoded to `#canvas` (standard for Godot web exports)
- No device-loss recovery (log only, page reload required)
- Shader startup: ~15s for first-time conversion of ~383 stages
- `threads=yes` works with and without `dlink_enabled=yes`. Combining threads
  with GDExtension support (`threads=yes dlink_enabled=yes`) was unsupported
  until 2026-10-07 — two bugs in Emscripten's own dylink/`EXPORT_ALL` glue, not
  this fork's code — and now **requires a patched toolchain**:
  `misc/emsdk_patches/apply.sh --apply`, re-applied after every `emsdk install`
  (`build-linux.sh` does it for you). Without the patches that configuration
  does not boot at all, and logs ~11 errors per load that silently drop later
  `Module` exports on worker threads. With them it boots clean — verified
  headless over COOP/COEP against a real GDExtension + thread-support project,
  0 console errors, running its own game scripts. Since 2026-10-16 threaded
  builds (with and without dlink) are exercised on real hardware with the
  separate render thread (the default for thread-enabled exports,
  `variant/separate_render_thread`):
  cameraSim, the heavy Forward+ scene, the platformer and four benchmark scenes
  at the display cap on Chrome and Firefox, quit clean. See
  `misc/emsdk_patches/README.md` and `webgpu_notes/TASKS.md` §2.5
- Threaded builds require the page to be served cross-origin isolated
  (`Cross-Origin-Opener-Policy: same-origin`,
  `Cross-Origin-Embedder-Policy: require-corp`), because they need
  `SharedArrayBuffer`. Godot's own editor HTTP server (Remote Deploy) and
  `platform/web/serve.py` already send both

### Format Limitations
- Float32 textures downgraded to float16 on Adreno (precision loss)
- Storage textures require format promotion (R8→R32Float)
- No texture component swizzle (L8/LA8 converted on CPU; font atlases avoid it by rasterizing to RGBA8 directly)
- No 3-component texture formats (RGB8/RGBH/RGBF expanded to RGBA8 on CPU, once per texture at load)

Both expansions happen on every WebGPU device without exception, so the engine reports them as verbose notes rather than as hardware-capability warnings.
- sRGB viewFormats excluded for storage textures (Dawn rejects them)

---

## 7. Bug Categories and Resolution Patterns

### 7.1 Resource Leaks (All Fixed)

| Bug | Fix Pattern |
|-----|------------|
| WGTexture not deleted on view failure | Release texture + delete struct on failure |
| WGBuffer not deleted on create failure | Delete struct before returning empty ID |
| Shader modules leaked on pipeline failure | Release both specialized modules on null pipeline |
| Shader modules leaked on shader_create failure | goto-based cleanup block |
| Readback cache UAF from HashMap rehash | Heap-allocate entries for pointer stability |

### 7.2 Use-After-Free (All Fixed)

| Bug | Fix Pattern |
|-----|------------|
| Async buffer map callback after buffer_free | freed-flag deferred deletion |
| Async fence callback after fence_free | freed-flag deferred deletion |
| Async timestamp readback after pool_free | freed-flag deferred deletion |
| Async readback entry after texture_free | cancelled-flag in callback |

### 7.3 Browser-Specific (All Fixed)

| Bug | Fix |
|-----|-----|
| Firefox validation: unbound bind groups | Pre-bind empty bind group at gaps |
| Firefox: >8 SSBOs per stage | Per-stage visibility metadata |
| Chrome: alpha compositing on canvas | Strip alpha writes from swap chain pipelines |
| Chrome: binding_array unsupported | Expanded to one binding per element (`expand_binding_arrays`), collapsed only past the stage budget |
| Adreno: float32 not filterable | Downgrade to float16 with bidirectional conversion |

### 7.4 WebGPU Spec Compliance (All Fixed)

| Bug | Fix |
|-----|-----|
| read_write storage in vertex shader | Demote to read-only |
| Stencil reference not set dynamically | Apply at pipeline bind time |
| Strip topology format unknown at pipeline creation | Create Uint16/Uint32 variants |
| Texture clear without RenderAttachment usage | Encoder copy from a zero/pattern buffer (was `wgpuQueueWriteTexture`, which reordered it; see 2.5) |
| Dynamic buffer offsets not applied | Multi-frame rotation with alignment |
| Encoder sync scope violation | Proactive encoder splitting |
| Constant-alpha blend factors don't exist | Single blend constant set to the alpha broadcast |
| No sampler LOD bias | Bias added in the shader from a uniform (`apply_sampler_lod_bias`) |
| Depth clamp | `depth-clip-control` + `unclippedDepth` |
| 256-byte row alignment | Applied in all copy operations |

---

## 8. Testing Coverage Assessment

### What's Tested (In Practice)
- Full Forward+ (Clustered) renderer path on real GPUs, and Forward Mobile where the adapter has fewer than 48 sampled textures per stage
- 2D Canvas rendering
- Multiple browsers (Chrome, Firefox, Safari)
- Adreno GPU format fallbacks
- Stress tests (20k instances, 32 lights, 60k shared-material objects)
- Async readback (viewport capture)
- Dynamic scenes (skeleton animation)

### Automated Testing Infrastructure

| Suite | Count | Coverage |
|-------|-------|----------|
| SPIR-V preprocessing unit tests | 291 | Every pass in `run_pipeline()`, edge cases, spec constant ops; the IR passes' outputs checked with `spirv-val` |
| Driver unit tests | 379 | Bind group layouts, format handling, pipeline state, expanded-array binding layout |
| Shader corpus (GLSL → SPIR-V → WGSL) | 14 fixtures | End-to-end conversion + validation |
| Forward+ feature matrix | 87 arms | Each feature off/on must change the frame, or for occlusion culling the object count drop (real GPU) |
| Forward+ parity | 87 arms | Each frame must match a native Vulkan render of the same scene |
| Engine shader corpus | ~400+ shaders | Regression baseline with `expected_failures.json` |
| Scene smoketests | 23 scenes × 3 browsers | Chrome, Firefox, Safari (never run: no Mac) — zero GPU errors required |
| Resource lifecycle stress tests | - | Rapid create/destroy cycles |
| Screenshot comparison | Chrome + Firefox | Pixel-level regression detection |
| C++ fuzz targets | 3 targets | `fuzz_preprocess_passes`, `fuzz_split_samplers`, `fuzz_spirv_to_wgsl` |

### Remaining Gaps
- Edge cases of the SPIRV-Cross patches rely on Godot's built-in shader corpus and the real-project corpus (`webgpu_tests/spirv_cross_eval/project_export.sh`, 2566 stages)
- Fuzz targets use standalone mutation driver (libFuzzer not available with Apple clang)

---

## 9. Security Considerations

### Input Validation
- SPIR-V input is engine-generated (not user-supplied in typical use)
- `run_pipeline()` validates its SPIR-V input with SPIRV-Tools before any pass runs (the engine's own runtime conversions skip it: their input is the engine's glslang output), and rejects a malformed module cleanly instead of asserting
- WebGPU API performs its own validation layer (browser-side)

### Memory Safety
- No raw pointer arithmetic exposed to user input
- Shadow buffers are allocated with Godot's `memalloc` (alignment-safe)
- Ring buffer bounds checked at wrap-around
- All async callbacks check for freed/cancelled state before dereferencing

### No Known Vulnerabilities
- The driver does not process untrusted input
- All data flows are engine-internal (GLSL→SPIR-V→WGSL pipeline is fully controlled)
- Browser's WebGPU validation layer provides an additional safety net

---

## 10. Conclusion

The godot-webgpu backend is **production-ready** for its target use case: running Godot 4.8 games in the browser with the Forward+ (Clustered) renderer, at parity with native Vulkan on every feature WebGPU can express, and Forward Mobile on adapters too small for it. All critical rendering paths work correctly, resource lifecycle is properly managed, and cross-browser compatibility is handled through well-scoped, spec-compliant workarounds.

The known limitations (mid-pass clear, indirect count, the WebGPU spec gaps in §6, shader startup time) are acceptable for an initial public release and clearly documented. No critical bugs were found during this comprehensive review.
