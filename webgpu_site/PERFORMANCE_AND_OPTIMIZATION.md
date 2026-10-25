# Godot WebGPU Backend — Performance & Optimization

## The Core Problem

WebGPU on the web runs inside a browser sandbox where every GPU API call crosses an inter-process communication (IPC) boundary. The cost structure:

| Environment | Cost per GPU call | Mechanism |
|------------|-------------------|-----------|
| Native (Vulkan/Metal/D3D12) | ~5 ns | Direct memory-mapped command buffer write |
| WebGPU (Chromium, desktop) | ~200-500 ns | WASM→JS context switch → Mojo IPC → GPU process |
| WebGPU (Chromium, mobile) | ~500-1500 ns | Same + slower IPC on mobile SoCs |

This 40-250x per-call overhead means Godot's forward mobile renderer — designed around "commands are free" — becomes **IPC-bound, not GPU-bound**. The GPU sits idle waiting for commands to arrive through the serialization pipeline.

---

## The Optimization Philosophy

A clear hierarchy drives all performance work:

1. **Eliminate calls entirely** — batching, merging, deduplication
2. **Reduce call frequency** — state caching, dirty tracking
3. **Reduce per-call cost** — command buffering (explored, parked)

All optimizations are gated behind `ApiTrait` enums queried at initialization, making them **WebGPU-only with zero impact on Vulkan/Metal/D3D12** code paths. This is a clean extension mechanism that avoids polluting the core renderer.

---

## The Journey from 3.25x Slower to Parity

### Benchmark: Shadow Stress Scene (20k instances, 32 omni lights)

| Phase | Key Changes | Frame Time | vs Native |
|-------|-------------|-----------|-----------|
| Baseline | None | 133 ms | 3.25x slower |
| +Staging fixes | Dirty tracking, re-dirty fix | ~120 ms | ~2.9x |
| +Dual-paraboloid + pass merging | Force DP, merge same-FB | 76 ms | ~1.86x |
| +Shadow instance batching | Merge same-mesh shadow draws | 57 ms | ~1.39x |
| +Skeleton atlas | Single writeBuffer per frame | ~48 ms | ~1.17x |
| +firstInstance dedup | Skip push constant IPC | ~46 ms | ~1.12x |
| +Color pass batching | Merge color draws | ~36 ms | **Parity** |

These numbers were measured while dual-paraboloid omni shadows were forced. That override was removed on 2026-10-17 for parity with native (see Layer 1); a scene with 32 shadowed omni lights left on the default cube mode now pays for 6 passes per light, and has not been re-measured.

### IPC Crossings Per Frame (3D Platformer)

| Stage | Crossings/Frame | Cost at 0.3us each |
|-------|----------------|-------------------|
| Baseline | ~23,837 | 7.15 ms (23.6% of frame) |
| After all optimizations | ~8,700 | 2.6 ms (11% of frame) |

---

## Layer 0: Staging Buffer Architecture

### Problem

Godot's `RenderingDevice` uses staging buffers for all CPU-to-GPU transfers. On Vulkan/Metal, these are GPU-mapped memory — idle blocks cost nothing. On WebGPU, the driver emulates staging via "shadow buffers" (CPU-side `memalloc` allocations). Three pathologies emerged:

1. **Unbounded pool growth**: Default 128MB cap allowed 69 blocks x 256KB = 17.7MB to accumulate during loading
2. **Full-buffer flush on unmap**: `buffer_unmap()` copied the entire shadow buffer (up to 32MB) to GPU via `wgpuQueueWriteBuffer` on every call, even if only a few bytes changed
3. **Re-dirtying after flush**: End-of-frame flush loop re-mapped buffers, unconditionally setting `map_dirty=true`, causing the next frame to flush ALL staging blocks

### Solution 1: 16MB Pool Cap

`API_TRAIT_STAGING_BUFFER_MAX_SIZE_MB` returns 16 for WebGPU. The `RenderingDevice::initialize()` method clamps `upload_staging_buffers.max_size` to this value. Overflow is handled by the existing stall-and-reuse path.

**Why 16MB**: Generous for per-frame dynamic updates (uniforms, instance data, bones). Large texture uploads go through the direct-write path (see below).

### Solution 2: Dirty Range Tracking

Added `dirty_offset`/`dirty_end` fields to `WGBuffer`:

```cpp
uint64_t dirty_offset = 0;
uint64_t dirty_end = 0; // 0 means "no explicit range set"
```

Three key changes:
- `buffer_unmap()`/`buffer_flush()` only flush the dirty range
- `buffer_persistent_map_advance()` sets dirty range to current frame's slice only
- Copy operations clear `map_dirty = false` (their transfer handles the flush)

**Impact**: Texture creation dropped from 10-25ms to 1-4.6ms. Per-frame uniform flushes write only the active frame slice.

### Solution 3: Eliminate End-of-Frame Re-Dirtying

The `_end_frame()` staging flush loop previously called `buffer_map()` after each `buffer_unmap()` to keep `data_ptr` valid. This unconditionally set `map_dirty = true`, causing the next frame to flush ALL 69 blocks (17.7MB) even if none were written.

The fix removes the re-map call entirely — shadow buffers and `data_ptr` persist after unmap (they're CPU allocations, not GPU mappings).

**Impact**: Max frame time dropped from 83-100ms to 32ms. The recurring ~82ms spike was eliminated. Staging flush cost: 2-54ms → 0.25ms (200x improvement).

### Solution 4: Mapped-at-Creation for Initial Buffer Data

New `buffer_create_with_data()` creates buffers with `mappedAtCreation = true`, memcpys data directly into the mapped range, then unmaps. This eliminates:
- Transfer worker staging buffer allocation
- `wgpuQueueWriteBuffer` calls (each ~9ms WASM-to-JS overhead)
- Command encoder copy from staging to destination

Applied to all five buffer creation paths: vertex, index, storage, texture, uniform.

---

## Layer 1: Shadow Pass Merging + Forced Dual-Paraboloid

### Problem

Each OmniLight3D with cubemap shadows generates 6 render pass encoder cycles + 2 copy-to-atlas operations. With 32 omni lights: ~192 cubemap passes + 64 copies + 4 directional splits = ~260 render pass operations per frame.

### Solution Part 1: Force Dual-Paraboloid Shadows

`API_TRAIT_FORCE_OMNI_DUAL_PARABOLOID` overrides `light_omni_get_shadow_mode()` to always return `SHADOW_DUAL_PARABOLOID`. 2 passes per light instead of 6+2.

**Quality trade-off**: Dual-paraboloid shadows have a seam at the hemisphere equator and slightly different sampling distribution. Acceptable for web where performance is the bottleneck.

> **Reverted 2026-10-17 (`webgpu_notes/TASKS.md` §4.29).** The trait now returns 0, so each OmniLight3D uses its own `omni_shadow_mode` -- cube by default, exactly as on native. Forcing dual-paraboloid made every project's omni shadows differ from native and ignored a setting the user had chosen; the Forward+ parity sweep counted it as a parity bug. The cost comes back for scenes with many shadowed omni lights (6 passes per light instead of 2; Part 2's pass merging still applies). The 133 → 76 ms figure below was measured with both parts together and has **not** been re-measured with cube shadows; the scene smoketest and `perf_guard` stayed green. To get the cheaper shadows on web, set `omni_shadow_mode = OmniLight3D.SHADOW_DUAL_PARABOLOID` on the lights that need it -- it is a per-light choice, as on every other platform.

### Solution Part 2: Merge Same-Framebuffer Shadow Passes

Pre-clear the positional shadow atlas once upfront, then render all positional shadow passes with `clear_region = false`. In `_render_shadow_end()`, consecutive passes sharing the same framebuffer are batched into a single render pass with viewport/scissor changes.

```
Before: 196 render pass begin/end cycles per frame
After:  4 render pass begin/end cycles per frame
```

**Impact**: Frame time: 133ms → 76ms (-43%).

---

## Layer 2: Instance Batching

### Shadow Pass Instance Batching

20,000 instances sharing the same mesh surface and shadow material previously issued 20,000 individual draw calls (40,000 IPC crossings: push constant + draw each).

The batching lookahead in `_render_list_template` detects consecutive draws sharing:
- Same mesh surface (shadow variant)
- Same material uniform set
- Same LOD index
- Same cull variant

When detected, they merge into a single instanced draw: `drawIndexed(indexCount, batchCount, ...)`.

The shader computes the correct per-instance data index:
```glsl
batch_instance_index = draw_call.instance_index + uint(gl_InstanceIndex);
```

**Impact**: 20,000 draws → 1 instanced draw. Frame time: 76ms → 57ms (-25%).

### Color Pass Instance Batching

Extended the same lookahead to opaque color passes with additional state checks:
- Same mesh surface (color variant)
- Same material uniform set
- Same cull mode, lightmap usage
- Same pipeline specialization (light counts, projector, soft shadow flags)
- Same transforms uniform set

**Safety exclusions** (zero-cost compile-time template checks):
- Transparent pass excluded (alpha-sorted draws must preserve back-to-front order)
- Skinned meshes excluded (per-instance vertex buffers)
- Multimesh excluded (uses gl_InstanceIndex for its own indexing)
- Particle instances excluded

**Impact (60k shared-material instances)**: Draw calls: 32,190 → 14 (-99.96%). FPS: 20.5 → 27.6 (+34.6%).

---

## Layer 3: firstInstance + Push Constant Deduplication

### Problem

Even after batching, non-batchable draws (different meshes/materials) still require per-draw `SetBindGroup` for the push constant dynamic offset — this is the remaining per-draw IPC that instance batching cannot eliminate.

### Solution

Encode the per-draw `base_index` in `drawIndexed`'s `firstInstance` parameter instead of the push constant:

```glsl
// Shader recovers: 0 + firstInstance = correct base_index
batch_instance_index = draw_call.instance_index + uint(gl_InstanceIndex);
// draw_call.instance_index = 0 (in push constant)
// gl_InstanceIndex = firstInstance value from draw call
```

After moving `base_index` out, compare remaining push constant fields via `memcmp`. If unchanged from previous draw, skip the `SetBindGroup` call entirely:

```cpp
push_constant.base_index = 0; // Move to firstInstance
bool need_pc = !pc_set_for_current_pipeline;
if (!need_pc && have_prev_fi_push_constant) {
    need_pc = memcmp(&push_constant, &prev_push_constant, size) != 0;
}
```

**Key insight**: Consecutive draws often share the same ubershader specialization (quantized light counts, feature flags). With `base_index` moved out, their push constants become identical.

**Impact (20k instances)**: Push constant writes: 11,610 → 5 per frame (-99.96%). FPS: 30.2 → 31.3 (+3.6%).

**Interaction with batching**: These are complementary, not conflicting:
- When draws CAN be batched (same mesh/material): merge into instanced draw (1 IPC total)
- When draws CANNOT be batched: firstInstance dedup skips the PC SetBindGroup (1 IPC instead of 2)
- `batch_count == 1` is required for the firstInstance path (prevents conflict)

---

## Skeleton Atlas: Single-Buffer Bone Upload

### Problem

Per-skeleton GPU buffer updates generate one `wgpuQueueWriteBuffer` call per skeleton per frame. In animation-heavy scenes: ~4000 IPC crossings.

### Solution

Single shared GPU buffer ("skeleton atlas") holds all bone data. Per-frame:
1. Walk dirty skeleton list, memcpy each skeleton's data into CPU mirror at its offset
2. Track `atlas_dirty_min`/`atlas_dirty_max` across all dirty skeletons
3. Single `buffer_update_direct()` covering the dirty range

Shader gains `bone_offset` push constant (repurposed padding field):
```glsl
uvec2 bones_01 = uvec2(bones.x & 0xFFFF, bones.x >> 16) * 2 + params.bone_offset;
```

**Memory**: Power-of-two growth (minimum 64KB, doubling on overflow). Bump allocator only — no deallocation (acceptable for typical fixed-skeleton-count scenes).

**Impact**: ~4000 IPC crossings → 1. Frame time: -17%.

---

## Texture Upload Optimization

### Batched Layered Texture Uploads

New `command_copy_buffer_to_texture_layered` collapses N per-layer `wgpuQueueWriteTexture` calls into one call with `extent.depthOrArrayLayers = N`. WebGPU supports writing multiple array layers in a single call when consecutively laid out in memory.

### Direct CPU→GPU Texture Write

`API_TRAIT_TEXTURE_INITIALIZE_DIRECT_WRITE` + `texture_initialize_direct_layered` eliminates the transfer worker entirely for Texture2DArray uploads:

```
Old: memalloc → pack → transfer worker staging buffer (GPU) → command encoder copy → GPU texture
New: memalloc → pack → single wgpuQueueWriteTexture from CPU pointer → memfree
```

No transfer worker, no GPU staging buffer, no command encoder, no pipeline barriers.

**Impact**: `create_from_images` total: 2877ms → 240ms (-92%). Peak VRAM per upload: -300MB.

---

## Redundant State Caching

The `WGCommandBuffer::RenderState` tracks currently-bound state:

```cpp
WGPURenderPipeline *current_pipeline = nullptr;
WGPUBuffer current_index_buffer = nullptr;
uint64_t current_index_offset = 0;
WGPUBuffer current_vertex_buffers[8] = {};
uint64_t current_vertex_offsets[8] = {};
```

Each bind call compares against cached state and skips the `wgpu*` call when unchanged. State resets at render pass begin and subpass transitions.

Impact is minimal with unique materials but meaningful with shared-material scenes where consecutive draws bind the same pipeline/vertex buffers.

---

## Command Buffering (Explored, Parked)

### Concept

Record all render pass encoder commands into a flat buffer (48 bytes/command, 16k capacity = 768KB) in WASM linear memory. Replay via single `EM_JS` call per render pass:

```javascript
for (var i = 0; i < count; i++) {
    switch (heap[base]) {
        case 0: pass.setPipeline(obj[heap[base + 1]]); break;
        case 1: pass.setBindGroup(...); break;
        // 12 command types total
    }
    base += CMD_U32;
}
```

### Results

- **M3 Ultra desktop**: Neutral (trampoline savings ~40ns/call offset by JS replay loop ~40ns/iteration)
- **Expected benefit on mobile/Chromebook**: 3-5x where trampoline is proportionally more expensive

### Decision

Parked for future use. Not merged because it adds complexity without measurable benefit on current target hardware. The optimization layers above (batching, firstInstance) already reduced per-frame crossings from ~23,837 to ~8,700, making the remaining trampoline overhead a smaller fraction.

---

## How the Optimizations Interact

```
Layer 0: Staging fixes + Direct Write     — eliminates loading-time and per-frame overhead
Layer 1: Shadow Pass Merging + DP mode    — reduces render pass count (additive with Layer 0)
Layer 2: Instance Batching                — reduces draw count within passes (builds on Layer 1)
Layer 3: firstInstance + PC dedup         — reduces per-draw overhead for non-batchable draws
```

**Layer 1 enables Layer 2**: Shadow pass merging puts all shadow draws into a single render pass, making consecutive same-mesh draws sort-adjacent — a prerequisite for effective batching.

**Layer 2 and Layer 3 are complementary**: Instance batching handles same-mesh draws; firstInstance handles different-mesh draws. They never conflict (`batch_count == 1` required for firstInstance).

**Layer 0 is orthogonal**: Staging fixes address loading-time and per-frame data transfer overhead, independent of draw call optimizations.

---

## API Trait Summary

| Trait | Value | Purpose |
|-------|-------|---------|
| `STAGING_BUFFER_MAX_SIZE_MB` | 16 | Cap staging pool heap waste |
| `BUFFER_CREATE_MAPPED_AT_CREATION` | 1 | Zero-copy initial buffer data |
| `TEXTURE_INITIALIZE_DIRECT_WRITE` | 1 | Bypass transfer worker for textures |
| `SKELETON_BUFFER_DIRECT_WRITE` | 1 | Direct queue write for bone atlas |
| `FORCE_OMNI_DUAL_PARABOLOID` | 0 | Was 1 (reduce shadow pass count) until 2026-10-17; see Layer 1 |
| `BATCH_INSTANCE_DRAWS` | 1 | Merge same-state draws |
| `FIRST_INSTANCE_INDEX` | 1 | Encode base_index via firstInstance |

All traits default to 0 in the base class. Non-WebGPU backends are completely unaffected.

---

## Performance Diagnostics

### Built-in Counters (logged every second)

- `draws`: Total draw calls issued
- `set_bind_group` / `set_bind_group_skipped`: Bind group calls issued vs deduplicated
- `push_constant_writes` / `push_constant_skipped`: PC ring writes vs skipped via dirty-check
- `render_passes`: Total encoder begin/end cycles
- `bg_cache_miss`: BGL rebind cache misses
- `set_pipeline` / `set_vertex_buffer`: Pipeline/VB bind calls
- `gap_bind_group`: Empty bind group fills for Firefox compatibility
- `first_instance_draws`: Draws using firstInstance encoding

### SPIR-V Cache Diagnostics

- Hit/miss counters per conversion
- Total conversions logged during startup

---

## 2026-10-16: GPU Fixes and the Separate Render Thread

One day of work on a real GPU (RTX 4080 SUPER, 165 Hz, Chrome), measured before and after on the user's cameraSim project, the heavy Forward+ stress scene, the 3D platformer demo and four benchmark scenes. *Main* is the main thread's CPU ms per frame at the 165 fps cap, *render* the render thread's; *uncapped* is engine fps with Chrome's frame-rate limit and vsync lifted; *GPU* is GPU busy ms per frame. Noise is about ±5%.

What changed:

- **SSIL constant local arrays hoisted** by a new SPIR-V pass (`hoist_constant_local_arrays`): Tint, the converter then, was materialising the kernel's constant arrays per invocation; the pass took SSIL's GPU time from 0.6 ms to under 0.05. SPIRV-Cross, the converter since October 2026, emits such arrays as module-scope `const`s instead of Tint's `var<private>`; SSIL's GPU time did not change with it, and a patch making them `var<private>` measured no different.
- **Read-write storage textures bound as `read_write` per format** (the old check queried a WGSL language feature as a device feature and always failed): the heavy scene's GPU time 2.63 → ~1.5 ms.
- **An upstream Godot shadow-atlas bug** (`light_storage.cpp`): when a reallocation found no slot, the light's version was never stored, so every positional shadow was redrawn every frame after any long early frame — which on the web is every frame. Heavy scene CPU −37%. Fix branch: `shadow-atlas/store-version-when-realloc-fails`.
- **No-op uniform-set prepares skipped** on WebGPU (new `API_TRAIT_UNIFORM_SET_PREPARE_FOR_USE`) and a 4-entry front on the bind-group rebind cache.
- **Web Audio writes skipped when unchanged** (the engine resends every playing sample's gains and pitch each frame): platformer audio 0.148 → 0.029 ms/frame. Upstream's sample-driver rewrite (godotengine/godot#123996) is cherry-picked with the cache re-applied. Since 2026-10-18 (`webgpu_notes/TASKS.md` §4.30) the unchanged values are skipped in C++, before any JS call, and the driver uses integer handles instead of ObjectID strings: **0.029 → 0.002 ms/frame**. The same work removed a per-playback worklet that posted a message every 128 frames (positions now come from the context clock) and a full copy of the decoded sound on every play.
- **A separate render thread**, on by default for thread-enabled exports (export option `variant/separate_render_thread`, which passes `--render-thread separate`; off, the project setting `rendering/driver/threads/thread_model` decides): rendering runs on its own pthread with its own WebGPU device, the canvas transferred as an `OffscreenCanvas`; the main thread never blocks on it (a blocking wait on the browser main thread is a busy-spin), waiting instead with `Atomics.waitAsync`; the render thread yields to its event loop through a `MessageChannel` message (never a timer: Chrome clamps nested `setTimeout(0)` to 4 ms). Browsers without WebGPU in workers fall back to the Safe model automatically. Design, traps and mechanics: `webgpu_notes/TASKS.md` §2.5.

| Scene | Main, before → after (nothreads) | → after, Separate (main + render) | Uncapped fps, before → after | GPU ms, before → after |
|---|---:|---:|---:|---:|
| cameraSim (real project) | 0.51 → 0.49 ms (−4%) | **0.09** + 0.44 ms (**−82%**) | 837 → 832 (−1%) | 0.64 → 0.58 (−9%) |
| Heavy Forward+ (scene_i) | 1.93 → 1.15 ms (**−40%**) | **0.06** + 1.18 ms (**−97%**) | 229 → 292 (**+28%**) | 3.40 → 1.46 (**−57%**) |
| 3D platformer demo | 0.84 → 0.79 ms (−6%) | **0.36** + 0.50 ms (**−57%**) | 833 → 821 (−1%) | 0.40 → 0.42 (+5%) |
| PBR Sphere | 0.28 → 0.27 ms (−4%) | 0.05 + 0.22 ms (−82%) | 1012 → 1022 (+1%) | 0.28 → 0.28 (0%) |
| GPU Particles | 0.26 → 0.27 ms (+4%) | 0.05 + 0.19 ms (−81%) | 982 → 974 (−1%) | 0.13 → 0.14 (+8%) |
| Other Effects (SSAO + Bloom) | 0.96 → 0.99 ms (+3%) | 0.09 + 0.99 ms (−91%) | 633 → 619 (−2%) | 0.50 → 0.49 (−2%) |
| Shadows | 0.44 → 0.40 ms (−9%) | 0.06 + 0.36 ms (−86%) | 846 → 867 (+2%) | 0.34 → 0.34 (0%) |

Averaged over the seven scenes the fixes alone are main thread −8%, GPU −8%, uncapped fps +4% — concentrated on the scenes that had something to fix, the rest already at their floor. With the render thread the main thread's per-frame cost falls −82% on average; what remains is the game's own logic (the platformer's 0.36 ms is physics, scripts and audio). Uncapped fps cannot be measured in Separate mode — Chrome's uncapped flags drive the rAF of a page whose canvas is worker-owned at ~60 Hz — and with vsync every configuration runs at the display rate. The Safe thread model measures the same as nothreads, and threads+GDExtension (dlink) the same as threads.

---

## 2026-10-17: The First Phone (Mali-G615, Chrome/Android)

Everything above was measured on desktop GPUs, where this backend's problem was always the CPU side of the browser. The first phone run (Motorola Razr 60: MediaTek MT6878, 2-core Mali-G615 MC2, Android 16, Chrome 154; the user's cameraSim project — Forward+, bokeh DOF near and far, a directional light and three shadow-casting spots, seven opaque draws, 1920x1080 backbuffer) inverted that picture completely.

**It is GPU-bound.** 20.5 fps; main thread 29% busy; **48.7 ms of GPU time per frame, 90% of the GPU timeline** (timestamp queries per pass, the same instrumentation as `gpu_time.mjs`, driven over USB by the tools in `webgpu_tests/perf_guard/android/`):

| GPU ms/frame | Pass |
|---:|---|
| 28.0 | opaque lighting pass (`SceneForwardClusteredShaderRD:18`, 7 draws → 1920x1080 RGBA16F); the depth prepass of the same 7 draws is 0.67 |
| 14.5 | bokeh DOF, four compute passes at full resolution |
| 1.2 | tonemap |
| ~1.7 | the 4096² shadow maps |
| < 1 each | cluster build, blit, 113 UI draws, copies |

Against the desktop profile of the same scene (opaque 0.29 ms, DOF 0.145) both big passes are ~100x — the hardware ratio, not one pathological shader.

**What does not matter on this phone** (all 19–21 fps): the debug vs release template, `dlink`, threads, the separate render thread (the user's own observation), ASTC-compressing the character texture. Cheaper shadows (2048 maps, soft filter quality 1) buy 10%; a cheaper bokeh (Very Low, Box) 4% — the DOF cost is fixed full-resolution bandwidth, not sample count.

**What matters is 3D pixel count**, almost exactly: `rendering/scaling_3d/scale` 0.75 → 30.1 fps, 0.66 with the cheap DOF/shadows → 50.1, 0.5 → 59.6 (the display cadence). FSR 1.0 upscaling undoes it (50.1 → 30.1: EASU+RCAS are 7.3 ms at 1080p output plus a 1.2 ms copy). The project now carries `.web_android` overrides (scale 0.6, bilinear, 2048 maps, filter quality 1, cheap bokeh) and runs at **55.6 fps**; `.web_ios` carries milder, untested ones. The web platform exposes `web_android` / `web_ios` feature tags, never `mobile`, so a project's `*.mobile` overrides do not apply in phones' browsers.

**Bisecting the lighting pass** — the three lit materials' fragment modules were dumped from the device, edited by function signature (Tint, the converter then, mangled names but kept parameter names), and substituted by exact code match; the unedited control reproduces 26.9 ms:

| Cut in the fragment WGSL | `:18` ms |
|---|---:|
| none (control) | 26.9 |
| fragment body returns at once (raster + RGBA16F write floor) | 1.85 |
| `light_compute` (the BRDF) returns at once | 8.4 |
| every `textureSampleCompare` → 1.0 | 16.4 |
| omni/spot processing returns at once (the three spots) | 15.6 |
| spot shadow samplers → 1.0 | 18.6 |
| directional shadow samplers → 1.0 | 23.0 |
| area-light and reflection-probe code returns at once | 27.2 |

So the pass is BRDF + 8-tap PCF for four lights per pixel, and the cuts are super-additive (BRDF 18.5 + shadows 10.5 > 25): part of the cost is register pressure/occupancy of the whole clustered program, which the texture-latency-bound shadow taps need. Every candidate for a cost of *ours* was then checked and cleared: `:18` is the specialized colour pass (not the ubershader fallback); the override constants reach Dawn; unused feature paths are free; `OpImageGather` is translated natively; and **f16 is a null result on this GPU** — the Forward+ shader was bridged to `EXPLICIT_FP16` (Mobile's lighting include already takes `half`; Clustered's locals are float, so the bridge casts at the six lighting calls, `F0`, `blur_shadow`, `reflection_process` and the clearcoat helpers; it compiles for every optional material path and Tint, the converter then, emitted `enable f16;`), and substituted with the device requesting `shader-f16`: 47.9 ms GPU vs a 48.7 ms f32 control. The bridge is kept as `webgpu_tests/perf_guard/android/forward_clustered_explicit_fp16_bridge.patch` for Apple GPUs, where f16 matters much more. Forward Mobile, for the record, settles at 21.6 fps here (opaque pass 34.1 ms for 4 draws — it has no depth prepass, so it shades overdraw).

Still open: a native-Vulkan number on the same phone (needs an Android SDK for an APK export) and profiling exports that use the separate render thread (its pthread worker never answers CDP evaluates; `android/worker_gpu.mjs` is the unfinished self-driving variant).

---

## 2026-10-20: The Load — What Ships Before the First Frame, and What Compiles

Three changes aimed at time-to-first-frame rather than frame time (`webgpu_notes/TASKS.md` §2.27, §2.18, §2.4).

**The startup stall was the GPU process.** Every earlier startup measurement looked at the page's main thread,
where the WebGPU calls are cheap. A trace of both processes with Dawn's categories
(`webgpu_tests/startup_phases/gpu_process_trace.mjs`) showed the GPU process's main thread compiling 89 compute
pipelines (~5.9 s on swiftshader) and parsing ~500 shader modules before the first frame, while the page's main
thread sat idle ~4.9 s between startup steps: the startup advances one step per animation frame, and Chrome
compiles WebGPU work on the thread that produces frames. The first frame used none of those pipelines and ~5% of
the modules -- the renderer creates every effect's compute pipelines at init and loads every variant of every
shader. The driver now creates both on first use:

| platformer, `callMain` → first frame (swiftshader, Forward+) | ms |
|---|---|
| before | 6,787 / 6,734 / 6,872 |
| lazy compute pipelines | 3,265 / 3,086 / 3,055 |
| + lazy shader modules | **2,775 / 2,857** |

So that an effect turned on later does not hitch instead, the deferred compute pipelines are compiled in the
background once the game runs (`createComputePipelineAsync()`, which Chrome compiles off the frame-producing thread;
one per frame). On the platformer 150 of 184 were ready within a minute and 5 had to compile on first use.

**Render pipelines off the frame thread.** Render pipelines were still created with `createRenderPipeline()`, and a
Forward+ variant takes ~0.5 s to compile on a desktop GPU (RTX 4080), on that same thread: a user's game froze for
up to 0.8 s whenever a new surface or a decal came into view, and in bursts just after the first frame. The
ubershaders compiled while a scene loads and the specializations the renderer draws with the ubershader until they
are ready now go through `createRenderPipelineAsync()`, which Chrome compiles on a worker thread. On that game, on an
idle RTX 4080:

| GPU-process main thread | before | after |
|---|---|---|
| busy before the first frame | 800-820 ms | **550 ms** |
| synchronous render pipelines after it | 23, 1.34-1.36 s | **11, 0.17 s** |
| stall when looking around the level (3 runs) | up to 521 ms | none from pipelines |
| phone (Razr 60, Mali-G615): freeze when the scene first draws | 17-18 s | **7 s** |

Still synchronous: `createShaderModule` (WebGPU has no asynchronous form) and the compute pipelines the first frame
binds.

**Fewer shader bytes before the first frame.** Variant pruning drops shader groups the target can never select and
defers the ones the project was not found to use into a lazily fetched extras file: the platformer's startup shader
pack went 68.9 → 14.5 MB (10.7 → 1.84 MB gzipped), with 51.4 MB (8.7 MB gzipped) fetched as the engine starts.

**One texture family per device.** With both VRAM compression families exported, each ships in its own pack and the
shell fetches only the one the device samples, instead of every player downloading both.

What remains on the critical path is main-thread CPU, about a third of it V8 compiling wasm functions the first time
they run (`wasm.CompileLazy`); Chrome ships no compile hints for Wasm, and the wasm code cache did not change it on a
repeat visit.

## 2026-10-25: The Converter Swap (Tint → SPIRV-Cross), Measured on the GPU

SPIRV-Cross's WGSL backend replaced Tint as the SPIR-V → WGSL converter only after it was timed against Tint-baked exports of the same tree (`webgpu_notes/TASKS.md` §4.36). GPU ms per frame, medians over at least three launches in rotated order:

| Where | Scene | Tint | SPIRV-Cross |
|---|---|---|---|
| Chrome, RTX 4080 SUPER | heavy Forward+ | 1.47 | 1.43 |
| Firefox (Linux, wgpu/Vulkan), RTX 4080 SUPER | heavy Forward+ | 3.88 | 3.82 |
| Chrome, Razr 60 (Mali-G615) | Forward+ fixture | 16.9 | 16.8 |
| | heavy | 73.8 | 71.3 |
| | post-fx | 23.3 | 23.2 |
| | platformer | 29.6 | 28.7 |
| | stress | 28.8 | 28.1 |

The phone numbers are with SPIRV-Cross patch `0019` (no WGSL `for`: Mali ran a negative-start counted loop zero times, a correctness fix that also made it no slower). On desktop Chrome the Forward Mobile perf_guard scenes were equal except post-fx, +2% (0.014 ms). In Firefox post-fx ran 10% more fps with SPIRV-Cross, and shadows/pbr ~1.8% fewer at ~1000 fps, CPU-side with GPU time equal. Converting is also ~5-10× faster per module, which only matters for unbaked stages: a baked export converts nothing at load.

## Remaining Optimization Opportunities

1. **Command buffering on mobile**: Re-evaluate when mobile WebGPU is a primary target. Expected 3-5x trampoline savings on Chromebook/mobile. *Measured 2026-10-17:* on a Mali-G615 phone the main thread sits at 29% while the GPU is at 90%, so trampoline savings would not change the frame rate there; a weaker CPU with a stronger GPU (or a lighter scene) would be the case for it.

2. **Skeleton atlas compaction**: Add free-list or generational compaction for dynamic skeleton scenes.

3. **Material sort hints**: Optimize render list sort key to prefer same-mesh grouping within materials, increasing batch hit rates.

4. **Batch size cap**: Add configurable maximum (e.g., 4096 instances) for mobile GPU scheduling.

5. **Shader pre-compilation** — *done*, and since 2026-10-20 pruned and partly deferred (above): the export-time shader baker ships WGSL (`baked: N, translated: 0` in the console); the runtime path remains for what it misses. Original note: Run the SPIR-V → WGSL converter (then Tint) at export time and ship WGSL directly. Runtime conversion only for specialization constants that differ from defaults. Would eliminate the sometimes up to ~15s startup cost.

6. **Push constant ring sizing**: Monitor peak usage and right-size the 256KB ring buffer. Add runtime warning when approaching the 1024-slot limit.

7. **Texture upload streaming**: Split large atlas uploads across multiple frames to avoid frame spikes during scene transitions.

---

## Key Files

| File | Role |
|------|------|
| `drivers/webgpu/rendering_device_driver_webgpu.cpp` | All WebGPU-specific fast paths |
| `platform/web/display_server_web.cpp` | The separate render thread (`render_thread_prestart`, `_render_thread_wake`, `render_thread_wait_for_draw`) |
| `servers/rendering/rendering_server_default.cpp` | `external_render_thread_*` hooks, `sync_previous_draw()` |
| `drivers/webgpu/spirv_preprocess.cpp` | The SPIR-V passes, incl. `hoist_constant_local_arrays` |
| `drivers/webgpu/webgpu_objects.h` | WGBuffer dirty tracking, RenderState |
| `servers/rendering/rendering_device.cpp` | Staging cap, direct write, firstInstance plumbing |
| `servers/rendering/rendering_device_driver.h` | API trait definitions |
| `servers/rendering/renderer_rd/forward_mobile/render_forward_mobile.cpp` | Batching, firstInstance, shadow merging |
| `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp` | Skeleton atlas |
| `servers/rendering/renderer_rd/shaders/forward_mobile/scene_forward_mobile.glsl` | batch_instance_index |
| `servers/rendering/renderer_rd/shaders/skeleton.glsl` | bone_offset |
