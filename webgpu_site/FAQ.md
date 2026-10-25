# Godot WebGPU — Frequently Asked Questions

## General / Getting Started

### 1. What is godot-webgpu?

A complete WebGPU rendering backend for Godot Engine 4.6.2 that enables Godot games to run in the browser using the modern WebGPU API instead of the legacy WebGL. It implements Godot's `RenderingDeviceDriver` interface — the same abstraction used by the Vulkan, Metal, and D3D12 backends — targeting the browser's WebGPU API via Emscripten's emdawnwebgpu port.

### 2. Which Godot renderer does it use?

The **Forward Mobile** renderer. This is Godot's lighter-weight 3D renderer designed for mobile GPUs, which maps well to WebGPU's single-queue, limited-feature-set model. The Forward Clustered renderer (Godot's desktop-class renderer) is not supported and is saved for a future update.

### 3. What browsers are supported?

- **Chrome 113+** (and Chromium-based browsers: Edge, Opera, Brave)
- **Firefox 120+** (via wgpu backend)
- **Safari 18+** (Technology Preview; WebGPU support still maturing)

All major desktop browsers with WebGPU enabled. Mobile browser support is emerging (Chrome Android with WebGPU flag, Safari iOS 18+).

### 4. How do I build the WebGPU export template?

```bash
scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no
```

The `dlink_enabled=yes` flag enables Emscripten dynamic linking, which produces a main module (`godot.wasm`) and a side module (`godot.side.wasm`). This is required for the WebGPU export template.

Requirements:
- Emscripten 4.0.10+ (for the emdawnwebgpu port; this fork pins 6.0.9)
- No Rust toolchain needed (SPIRV-Cross's C++ WGSL backend is compiled directly into the engine)
- Standard Godot build dependencies (SCons, Python, C++ compiler)

### 5. Do I need to modify my Godot project to use WebGPU?

Almost nothing. Existing Godot 4.6 projects targeting the Mobile renderer will generally work without modification — shaders, materials, and scenes carry over as-is.

The one exception is **GPU readback**. Any code that reads data back from the GPU — including `RenderingDevice.texture_get_data()`, `Image.get_image()`, or compute shader result retrieval — must be adapted. On native backends (Vulkan, Metal, D3D12), these calls are synchronous and return data immediately. On WebGPU, the underlying `buffer.mapAsync()` is asynchronous and cannot resolve while C++ holds the WASM call stack, so the first call returns `null`/empty data. You must poll across frames until the result is available:

```gdscript
# Native (Vulkan/Metal): works immediately
var data = rd.texture_get_data(texture_rid, 0)

# WebGPU: must poll — first call returns null
var data = null
while data == null or data.size() == 0:
    await get_tree().process_frame  # unwind the call stack so the browser event loop can tick
    data = rd.texture_get_data(texture_rid, 0)
```

This 1+ frame latency applies to any GPU→CPU data transfer. If your project never reads back from the GPU (the common case for most games), no changes are needed.

The empty first result is the designed behavior, not an error, so the engine only notes it at `--verbose` level (add `--verbose` to `GODOT_CONFIG['args']` in the exported HTML to see it). If you are looking at a console from a build before 2026-10-17, it appeared as a `WARNING` -- which on web was logged with `console.error()` -- and it meant nothing more than that a readback had been started.

The only other user-visible difference is setting `rendering/renderer/rendering_method.web` to `mobile` or `forward_plus` (which maps to `mobile` on web) in project settings.

### 6. Why WebGPU instead of WebGL 2?

WebGPU provides an asynchronous GPU API, which prevents the frame-stalling pipeline bubbles that are impossible to avoid on WebGL — the browser can overlap CPU and GPU work instead of blocking on synchronous GL calls. Beyond that, WebGL 2 maps to OpenGL ES 3.0, which lacks compute shaders, storage buffers, and the programmable pipeline features that Godot 4's RenderingDevice architecture requires. WebGPU provides a modern GPU API comparable to Vulkan/Metal/D3D12, enabling Godot's full RD-based renderer to run in the browser for the first time.

---

## Performance

### 7. How does WebGPU performance compare to native?

On desktop browsers with the optimizations enabled, WebGPU achieves **close to parity with native Vulkan/Metal** for typical Forward Mobile scenes — around 1.4x native frame times. The benchmark journey:
- Initial unoptimized: 3.25x slower than native
- After all optimizations: ~1.4x native

The key insight: WebGPU is IPC-bound (not GPU-bound), and our optimizations reduce IPC message count by 99%+ for common cases.

### 8. Why was WebGPU initially 3.25x slower than native?

Every WebGPU API call on the web crosses an inter-process communication boundary: WASM → JS context switch → browser GPU process (via Mojo IPC on Chromium). Each call costs ~200-500ns vs ~5ns on native. Godot's renderer, designed assuming "commands are free," issued ~24,000 GPU calls per frame — accumulating 7ms+ of pure IPC overhead.

### 9. How did you achieve near parity?

A layered optimization stack that reduces IPC message count:
1. **Staging buffer fixes**: Eliminated redundant 32MB flushes (200x improvement in staging cost)
2. **Shadow pass merging**: 196 render passes → 4 per frame (-43% frame time)
3. **Instance batching**: 20,000 draws → 1 instanced draw (-25%)
4. **Skeleton atlas**: 4,000 bone uploads → 1 per frame (-17%)
5. **firstInstance encoding**: Eliminates per-draw push constant IPC (-3.6%)
6. **Color pass batching**: 32,190 draws → 14 per frame (+34.6% FPS)

### 10. What's the startup time?

That 15-second figure was from before export-time baking, when ~383 SPIR-V stages were converted to WGSL in the browser at every load (by Tint then; SPIRV-Cross since October 2026). Now the editor bakes WGSL into the export (`shader_baker/enabled`), and a baked export converts nothing at load.

Three later changes cut what remains (`webgpu_notes/TASKS.md` §2.27, §2.18, §2.4):

- **Fewer shaders before the first frame.** `shader_baker/variant_pruning` leaves out shader groups the browser can never select, and moves the ones nothing in the project was found to use (Forward+'s TAA/FSR 2/lightmap/VoxelGI/SDFGI variants, when the project uses none of them) into `<name>.shaders_extra.bin`, which loads in the background once the game is starting. On the 3D platformer demo the shaders downloaded before the first frame went from 10.7 MB to 1.84 MB gzipped.
- **One texture family, not two.** With both desktop (BC) and mobile (ETC2/ASTC) texture compression exported, each family ships in its own pack and the browser downloads only the one its GPU samples.
- **Nothing compiled that is not used.** The driver creates shader modules and compute pipelines on first use. Chrome compiles them on the thread that also draws the page's frames, and the engine used to create every effect's pipelines and every shader variant's module at startup; on the platformer that took the time from engine start to first frame from 6.8 s to 2.8 s (measured on a software adapter, so real-GPU numbers are smaller).
- **No freeze when something new comes into view.** Render pipelines the renderer compiles in the background (a material's variants while its scene loads, and the specialized versions it swaps in for the ubershader) are created asynchronously, so Chrome compiles them on a worker thread instead of the one that draws the page. Before, each one could freeze a Forward+ game for about half a second on a desktop GPU.

What is left is ordinary main-thread work: engine initialization, scene loading, and the browser compiling the engine's WebAssembly functions the first time they run.

### 11. Are there any performance regressions vs native I should know about?

- **Omni light shadows**: each light now uses its own `omni_shadow_mode` (cube by default), as on native. Until 2026-10-17 dual-paraboloid was forced, which was cheaper (2 passes per light instead of 6) but did not match native. Scenes with many shadowed omni lights can get that speed back by setting `omni_shadow_mode = SHADOW_DUAL_PARABOLOID` on those lights
- **Shader startup**: ~15s first-time compilation (native has no equivalent cost)
- **Texture readback**: 1-frame async delay (native is synchronous)
- **No multi-threaded command recording**: single queue. Rendering runs on a separate thread in thread-enabled exports by default (export option `variant/separate_render_thread`; main thread −57% to −97% per frame on the measured scenes, see `PERFORMANCE_AND_OPTIMIZATION.md`), but it is one thread recording.
- **No GPU-driven rendering**: Indirect draw count always uses max (wastes some GPU cycles)

---

## Architecture / Design

### 12. How does the WebGPU driver fit into Godot's architecture?

It implements `RenderingDeviceDriver` — the same abstract interface that the Vulkan, Metal, and D3D12 backends implement. The driver is selected at startup based on project settings. From the renderer's perspective, it's just another backend — the Forward Mobile renderer doesn't know or care that it's running on WebGPU.

The driver advertises its capabilities via `ApiTrait` enums, and the rendering server conditionally activates optimized code paths based on those traits. This keeps platform-specific logic out of the core renderer.

### 13. How are push constants handled without native WebGPU support?

WebGPU has no push constants. We emulate them with a **256KB storage buffer ring**:

- 1024 slots x 256 bytes each (matching `minStorageBufferOffsetAlignment`)
- A bind group with `hasDynamicOffset=true` is bound once, and the offset advances per draw
- A CPU shadow buffer accumulates all push constant data during command recording
- A single `wgpuQueueWriteBuffer` flushes the dirty range at submit time (one IPC per frame)

This design means push constant updates cost zero additional IPC — they're batched into the one submit-time flush.

### 14. How does the shader pipeline work?

Godot compiles GLSL → SPIR-V at engine build time (via glslang). At runtime in the browser:

1. SPIR-V bytecode is loaded from the shader container
2. `spirv_preprocess::run_pipeline()` runs a fixed chain of C++ passes (`spirv_preprocess.cpp`, plus three on SPIRV-Tools' IR in `spirv_opt_passes.cpp`) that transform the SPIR-V for WebGPU compatibility: push constant rewrite, combined sampler split, Y-negation, texture-array expansion, the sampler mipmap bias, lowering of `isnan`/`isinf`/`textureQueryLod`, which WGSL has no direct form for, readonly inference, and more. The full order is in `TECHNICAL_REFERENCE.md` §4.1
3. SPIRV-Cross's WGSL backend (`thirdparty/spirv-cross-wgsl`, compiled directly into the engine WASM) parses the preprocessed SPIR-V and generates WGSL
4. Post-processing fixes browser-specific issues (f32::MAX literals, f16 stripping, derivative uniformity)
5. C++ applies format remapping
6. `wgpuDeviceCreateShaderModule` creates the GPU shader

Results are cached by 64-bit SPIR-V hash — identical stages are never converted twice.

### 15. Why SPIRV-Cross? Why vendor and patch it?

The converter changed twice. Naga (Rust, prebuilt WASM, 42 patched files) went in May 2026 for Tint, Google's SPIR-V→WGSL compiler from Dawn: C++ only, no Rust toolchain, compiled straight into the engine. In October 2026 Tint gave way to SPIRV-Cross's WGSL backend ([Ludicon/spirv-cross-wgsl](https://github.com/Ludicon/spirv-cross-wgsl), Apache-2.0/MIT), measured on everything before Tint was deleted (`webgpu_notes/TASKS.md` §4.36):
- **Drop-in after our passes** — all 276 engine variants convert, with bindings, binding kinds and override ids identical to Tint's
- **Faster and smaller** — ~5-10× faster per module; the converter CLI builds clean in 44 s instead of 63 s, a web template variant compiles ~31 s less; ~204k lines of vendored source deleted (1.8 MB vendored instead of Tint's 9.1 MB). The WGSL text is 6-9% larger, but the baked `index.shaders.pck` gzips 17% smaller (3.70 vs 4.43 MB)
- **Same image, same speed** — Forward+ 71/71 at parity with native Vulkan, the user's cameraSim project indistinguishable from Vulkan, desktop Chrome, Firefox and the Razr 60 phone as fast or faster on the GPU
- **Already a dependency** — the engine vendors SPIRV-Cross for Metal; the WGSL fork is a second copy in its own namespace (`spirv_cross_wgsl`) so both link into one macOS editor

Four small local patches, never upstreamed (`thirdparty/README.md`):
1. `0001` the barrier/load/barrier broadcast idiom as `workgroupUniformLoad` (FSR2 barrier uniformity)
2. `0006` 16-bit normalized storage formats (`CanvasSdfShaderRD`)
3. `0015` uniform compute builtins passed as parameters (contact shadows barrier uniformity)
4. `0019` no WGSL `for` statements (a Mali-G615 driver ran a negative-start `for` zero times)

### 16. How does the WebGPU device get created?

JavaScript creates the device **before** WASM loads (since device creation is async and Emscripten C++ can't await Promises without Asyncify). The `engine.js` shell:
1. Calls `navigator.gpu.requestAdapter()` with `powerPreference: 'high-performance'`
2. Requests a device with all supported optional features and maximum limits
3. Passes the device into the Emscripten module config
4. C++ imports it via `WebGPU.importJsDevice()`
5. The SPIR-V → WGSL converter is already compiled into the engine WASM — no separate shader converter to load

### 17. Why a single 7733-line implementation file?

This matches the Vulkan driver's structure — `rendering_device_driver_vulkan.cpp` is also a single 6598-line file. Both backends implement the same `RenderingDeviceDriver` interface in one `.cpp`. The WebGPU driver is actually *more* factored than Vulkan, having extracted `webgpu_objects.h` (429 lines) and `pixel_formats_webgpu.h` (705 lines) into separate files. The file is navigable due to clear `/* SECTION */` markers and extensive comments.

---

## Compatibility

### 18. What rendering features work on WebGPU vs native?

**Fully working**: Forward+ (Clustered) on adapters with 48+ sampled textures per stage, which includes the RTX 4080 desktop this was measured on and the Mali phone in Q20, and Forward Mobile otherwise. Forward+ is measured feature by feature against native Vulkan (`webgpu_tests/forward_plus --parity`, 71 features, all within tolerance): lights and shadows including omni cube shadows, SDFGI, VoxelGI, reflection probes, SSR/SSAO/SSIL, volumetric fog, glow, DOF, TAA/FXAA/MSAA/FSR2, `DEPTH_TEXTURE`, refraction, decals, particles, runtime-indexed texture arrays (lightmap and VoxelGI arrays use the same expansion but have no dedicated test). Plus 2D Canvas, skeleton animation, instance batching and texture compression (BC/ETC2/ASTC if the GPU supports it).

**Limited/degraded**: texture readback (async 1-frame delay), storage textures (format promoted), MSAA 2x/8x (run as 4x), SDFGI (no image atomics: a plain read-modify-write, 0.025 mean diff from native), FSR2 (fp16 resolve: edge-only differences), no sampler border colour, no wireframe. The full list is `webgpu_notes/TASKS.md` §2.31.

**Not available**: Subpass-based post-processing, multiview/VR, VRS, tessellation, geometry shaders, subgroups, half-float math, texture component swizzle (emulated on CPU for L8/LA8; monochrome font atlases are rasterized straight to RGBA8 instead, so they need no conversion), 3-component texture formats (RGB8/RGBH/RGBF gain an alpha channel at load).

### 19. Will my custom shaders work?

Yes, with caveats:
- Standard Godot shader language features work
- `modf`, `isinf`, `isnan` are automatically replaced with WGSL-compatible equivalents, and so is `textureQueryLod`
- Texture arrays (`uniform sampler2D tex[4]`) work, indexed at runtime, up to the stage's texture budget
- `POINT_COORD` draws the mesh as points (as on every platform); WebGPU has no point size, so each point is drawn as a quad
- Array varyings are decomposed into individual variables
- Switch statements are converted to if/else chains
- Canvas shaders: binding 0 in set 3 is reserved for push constant emulation (bindings shifted to 1-4)

If you use very advanced GLSL features or rely on specific Vulkan behaviors, test on WebGPU early.

### 20. Does it work on mobile browsers?

- **Chrome Android**: yes, measured on 2026-10-17 on a Motorola Razr 60 (Mali-G615 MC2, Chrome 154, no flags needed). Everything loads and renders correctly, and the adapter reports 48 sampled textures per shader stage, so a `forward_plus` project runs **Forward+** there, not Forward Mobile. Adreno GPUs may additionally trigger the float32→float16 texture downgrades (untested in this fork).
- **Safari iOS 18+**: untested in this fork; reported working upstream with the texture-array and format workarounds.
- **Firefox Android**: not yet shipping WebGPU.

For texture downloads, export with both `vram_texture_compression/for_desktop` and `for_mobile` on: with `separate_packs` (the default) a phone downloads only the ETC2/ASTC textures and a desktop only the BC ones.

What a phone is actually limited by is the GPU, not the browser overhead this backend spent most of its effort on: on that Razr a real Forward+ project at its 1920x1080 backbuffer is 20 fps with the main thread 29% busy and 48.7 ms of GPU time per frame (lighting pass 28 ms, bokeh DOF 14.5 ms). Template kind, dlink, threads, the separate render thread and texture compression make no difference; 3D resolution does — `rendering/scaling_3d/scale` 0.5 gives 60 fps. Use `.web_android` / `.web_ios` setting suffixes to lower the 3D scale, shadow map sizes and soft-shadow quality on phones only (the web platform never exposes the `mobile` feature tag), and keep bilinear upscaling (FSR 1.0 costs ~7 ms at 1080p output on that GPU). The full investigation, including the lighting-shader bisection and why an f16 build of the Forward+ shader did not help, is in `webgpu_notes/TASKS.md` §2.9; the on-device tooling is `webgpu_tests/perf_guard/android/`.

### 21. What about SharedArrayBuffer / COOP/COEP headers?

WebGPU does **not** require SharedArrayBuffer or cross-origin isolation headers. This means:
- Non-threaded WebGPU builds work on any hosting provider (GitHub Pages, Netlify, etc.)
- No special server configuration needed
- Only threaded builds need COOP/COEP — and they are what unlocks the separate render thread (on by default for such exports, `variant/separate_render_thread`), which moves rendering off the main thread

This is a significant deployment advantage over threaded web builds.

### 22. How does it handle browsers that don't support WebGPU?

The HTML shell (`full-size.html`) checks `navigator.gpu` on load. If WebGPU is unavailable, it displays a clear message directing users to update their browser. The engine never starts if WebGPU is missing.

For dual-renderer builds (`webgpu=yes opengl3=yes`), the export plugin can be configured to fall back to WebGL if WebGPU is unavailable.

---

## Technical Deep-Dives

### 23. How does the bind group rebinding cache work?

WebGPU requires bind group layouts to match exactly when binding a uniform set to a pipeline. When a uniform set created with shader A's layout needs to work with shader B's pipeline (common in Godot where one material is used with multiple shaders), the driver:

1. Checks if layouts are pointer-equal (fast path — same shader)
2. Checks the per-uniform-set rebind cache
3. On cache miss: adapts entries (adjusts sampler types, texture sample types, view dimensions), filters to target layout's bindings, creates a new bind group, caches it

This avoids re-creating bind groups every frame while ensuring WebGPU validation passes.

### 24. How does format promotion work for storage textures?

WebGPU only supports a limited set of storage texture formats (see spec section 26.1.1). When Godot creates a texture with `StorageBinding` usage and a format like R8Unorm:

1. **At texture creation**: format promoted to R32Float (actual GPU allocation)
2. **In WGSL shaders**: format name `r8unorm` replaced with `r32float` via string substitution
3. **On upload**: data converted from R8 to R32Float by `texture_upload_convert`
4. **On readback**: data converted back from R32Float to R8 by `texture_readback_convert`

This is transparent to the engine — it thinks it's working with R8, and the driver handles all conversion.

### 25. What's the encoder splitting mechanism for sync isolation?

WebGPU has implicit synchronization within a command encoder, but it cannot handle a texture being used as both a read (TextureBinding) and write (RenderAttachment) target within the same encoder without an explicit scope break.

When the driver detects a render pass attachment that has both usage flags, it:
1. Flushes the push constant shadow buffer
2. Finishes and submits the current command encoder
3. Creates a fresh encoder

This ensures the prior read operations complete before the write begins. It's conservative (splits even when no actual conflict exists in the current frame) but prevents WebGPU validation errors that would invalidate the entire command buffer.

### 26. How does async buffer readback work without blocking?

WebGPU's `mapAsync` returns a Promise that cannot be awaited from synchronous C++ (without Asyncify, which was abandoned). The solution is a frame-deferred pattern:

**Frame N**:
- Engine calls `texture_get_data()` or `buffer_get_data_direct()`
- Driver copies GPU texture/buffer to a staging buffer
- Initiates `wgpuBufferMapAsync` on the staging buffer
- Returns empty data (not ready yet)

**Frame N+1**:
- Engine calls again
- Callback has fired between frames (during `requestAnimationFrame`)
- Driver returns the mapped data from the staging buffer
- Initiates a fresh copy for the next request

The 1-frame latency is acceptable for viewport capture, profiling, and compute result readback.

### 27. How is the combined-sampler splitting implemented?

At the SPIR-V binary level (before conversion), the most complex preprocessing pass (~500 lines of C++):

1. Identifies all `OpVariable` with `OpTypeSampledImage` pointer type
2. For each combined variable at binding N:
   - Keeps original variable as the **image** (reuses original ID for reference stability)
   - Creates a new variable as the **sampler** at binding N*2
   - Image gets binding N*2+1
3. Rewrites all `OpLoad` of combined variables to emit separate image + sampler loads followed by `OpSampledImage`
4. Handles function parameters by rewriting call sites
5. Doubles ALL other bindings to avoid collision (except PC ring buffer at binding 120)
6. Updates `OpEntryPoint` interface lists

This approach is more robust than post-conversion text patching because it handles edge cases like multi-level function call chains and access chain expressions.

---

## For Godot Core Contributors

### 28. What changes were made to the base RenderingDeviceDriver interface?

10 new `ApiTrait` enum values and ~9 new virtual methods were added to `rendering_device_driver.h`. All new methods have safe default implementations (return invalid/false/empty), so non-WebGPU backends are completely unaffected.

New methods include: `buffer_create_with_data`, `buffer_initiate_async_map`, `buffer_write_direct`, `buffer_get_data_direct`, `buffer_flush`, `texture_get_gpu_pixel_size`, `texture_readback_convert`, `texture_upload_convert`, `texture_initialize_direct_layered`, `command_copy_buffer_to_texture_layered`.

### 29. What would need to change for upstream Godot acceptance?

1. **RFC for API traits**: The 10 new trait enums need formal review by Godot rendering team
2. **Vendored converter**: a second SPIRV-Cross (the WGSL fork, beside Metal's) with four local patches; by the project's rule the patches are not offered upstream, so upstream Godot would vendor the fork as patched here or wait for the WGSL backend to land in KhronosGroup/SPIRV-Cross
4. **Reduce WGSL text manipulation**: Move remaining string-level transforms into SPIR-V preprocessing where possible
5. **CI infrastructure**: Headless Chrome/Firefox WebGPU testing
6. **Shared constants**: Single source of truth for magic numbers (binding 120, ring size, alignment)
7. **Feature flags documentation**: Clear docs on what each ApiTrait does and when it's safe to enable

### 30. How does the instance batching interact with Godot's existing render list?

The batching is a lookahead in `_render_list_template` (Forward Mobile). After processing a draw, it peeks ahead at the next N draws checking if they share:
- Same mesh surface (same variant for the current pass)
- Same material uniform set
- Same LOD index
- Same cull variant
- Same pipeline specialization (light counts, features)
- Not transparent, not skinned, not multimesh, not particle

If all match, they merge into a single `drawIndexed(indexCount, batchCount)` call. The shader uses `draw_call.instance_index + gl_InstanceIndex` to index into per-instance data.

This works because Godot's render list is already sorted by shader → material → geometry, making same-state draws naturally adjacent.

### 31. How does the skeleton atlas integrate with the existing skeleton system?

When `supports_buffer_direct_write()` returns true (WebGPU only), the mesh storage system uses a shared GPU buffer instead of per-skeleton buffers:

- `_update_dirty_skeletons()` has two paths: atlas (new) and legacy (original)
- Atlas path: memcpy all dirty skeleton data into CPU mirror, track dirty range, single `buffer_update_direct()`
- Shader: `bone_offset` push constant (repurposed padding field) indexes into the atlas
- Skeleton compute shader adds `bone_offset` to all bone lookups

The atlas grows with power-of-two strategy (min 64KB). The uniform set is rebuilt lazily on buffer reallocation.

### 32. Is there a test suite? How is correctness validated?

Currently, there is no automated test suite for the WebGPU backend. Correctness is validated through:
- Manual testing across Chrome, Firefox, and Safari
- Stress test scenes (20k instances, 32 lights, 60k shared-material objects)
- The browser's own WebGPU validation layer (catches spec violations)
- SPIRV-Tools' validator on `run_pipeline()`'s input, and the browser's WGSL compiler on the output
- Performance counter monitoring for unexpected behavior

For upstream, we'd need:
- Headless browser CI with WebGPU (Chrome --headless + SwiftShader)
- Shader corpus tests (convert all built-in shaders, validate WGSL output)
- Screenshot comparison tests across browsers
- Resource lifecycle stress tests (rapid create/destroy cycles)
