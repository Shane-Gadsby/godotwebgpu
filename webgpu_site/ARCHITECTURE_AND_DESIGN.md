# Godot WebGPU Backend — Architecture & Design

## Executive Summary

The Godot WebGPU backend is a complete implementation of Godot's `RenderingDeviceDriver` interface targeting the browser's WebGPU API. It enables Godot 4.8 games to run in the browser with the Forward+ (Clustered) renderer, at parity with native Vulkan on every Forward+ feature WebGPU can express (82 matrix arms, `webgpu_notes/TASKS.md` §4.39), despite WebGPU's fundamentally different execution model. Adapters with fewer than 48 sampled textures per stage fall back to Forward Mobile, the renderer this backend first shipped with on Godot 4.6.2.

The implementation spans ~11,000 lines of driver-specific code across 8 files under `drivers/webgpu/`, plus ~6,000 lines of C++ SPIR-V preprocessing and SPIRV-Cross's WGSL backend (vendored and patched locally) linked directly into the engine WASM, ~1,200 lines of rendering server modifications, and ~400 lines of platform/web integration. It achieves functional parity with the Mobile renderer path while working within WebGPU's constraints: no push constants, no combined image-samplers, async-only buffer mapping, no explicit barriers, and limited format support.

---

## 1. High-Level Architecture

### System Diagram

```
+---------------------------------------------------------------------+
|                     Godot Engine 4.8                                 |
|                                                                       |
|   +---------------------------------------------------------------+  |
|   |          RenderingDevice (Abstract Interface)                  |  |
|   |   servers/rendering/rendering_device_driver.h                  |  |
|   +---------------------------------------------------------------+  |
|          |                    |                    |                   |
|   +------+------+    +-------+------+    +--------+-------+          |
|   | VulkanDriver|    | MetalDriver  |    | WebGPU Driver  |          |
|   | (desktop)   |    | (Apple)      |    | (THIS IMPL)    |          |
|   +-------------+    +--------------+    +--------+-------+          |
|                                                   |                   |
+---------------------------------------------------------------------+
                                                    |
+---------------------------------------------------------------------+
|                    WebGPU Driver Layer                                |
|                                                                       |
|   +---------------------------+  +------------------------------+    |
|   | RenderingContextDriver    |  | RenderingDeviceDriver        |    |
|   | WebGPU (context/device)   |  | WebGPU (7733 LOC impl)       |    |
|   |                           |  |                              |    |
|   | - WGPUInstance             |  | - Buffer management          |    |
|   | - WGPUAdapter             |  | - Texture management         |    |
|   | - WGPUDevice (from JS)    |  | - Shader creation (SPIR-V)   |    |
|   | - WGPUQueue               |  | - Pipeline creation          |    |
|   | - Surface management      |  | - Command recording          |    |
|   +---------------------------+  | - Push constant emulation    |    |
|                                  | - Bind group management      |    |
|   +---------------------------+  | - Swap chain management      |    |
|   | RenderingShaderContainer  |  +------------------------------+    |
|   | WebGPU (SPIR-V storage)   |                                      |
|   +---------------------------+  +------------------------------+    |
|                                  | spirv_preprocess.cpp (C++)   |    |
|   +---------------------------+  | - 12 SPIR-V binary passes    |    |
|   | webgpu_objects.h          |  | - Push constant rewrite      |    |
|   | - WGBuffer, WGTexture     |  | - Combined sampler split     |    |
|   | - WGShader, WGPipeline    |  | + SPIRV-Cross (SPIR-V→WGSL) |    |
|   | - WGCommandBuffer         |  +------------------------------+    |
|   +---------------------------+                                      |
|                                  +------------------------------+    |
|                                  | Platform Integration         |    |
|                                  | - engine.js (device init)    |    |
|                                  | - detect.py (build config)   |    |
|                                  | - display_server_web.cpp     |    |
|                                  +------------------------------+    |
+---------------------------------------------------------------------+
                          |
+---------------------------------------------------------------------+
|              Browser WebGPU API (via emdawnwebgpu)                    |
|   - navigator.gpu.requestAdapter() / requestDevice()                 |
|   - wgpuDeviceCreateBuffer, wgpuDeviceCreateTexture, etc.            |
|   - Single queue, implicit synchronization, no explicit barriers     |
+---------------------------------------------------------------------+
```

### Component Inventory

| Component | Files | LOC | Role |
|-----------|-------|-----|------|
| Device Driver | `rendering_device_driver_webgpu.h/cpp` | ~8,370 | Core: implements all RDD virtual methods |
| Context Driver | `rendering_context_driver_webgpu.h/cpp` | ~300 | Device/surface lifecycle from JS shell |
| Object Wrappers | `webgpu_objects.h` | 430 | GPU object wrapper structs |
| Shader Container | `rendering_shader_container_webgpu.h/cpp` | ~210 | SPIR-V storage + push constant metadata |
| SPIR-V Preprocessing | `spirv_preprocess.cpp/h` | ~5,100 | Binary-level SPIR-V rewriting passes, and `run_pipeline()`, the one pass order every converter uses |
| SPIR-V IR passes | `spirv_opt_passes.cpp/h`, `spirv_binding_layout.h` | ~1,150 | Passes on SPIRV-Tools' optimizer IR: texture-array expansion, sampler LOD bias, lowering of ops WGSL has no direct form for; the expanded-array binding layout shared with the driver |
| Converter wrapper | `spirv_cross_wrapper.cpp/h`, `spirv_to_wgsl.cpp/h` | ~180 | Runs SPIRV-Cross's WGSL backend on preprocessed SPIR-V (the only code built with wasm exceptions; a throw becomes a failed conversion); `spirv_to_wgsl` is the runtime entry point |
| WGSL text scans | `wgsl_text_scan.cpp/h` | ~1,430 | Everything the driver reads out of the converted WGSL (bind-group layout facts, override ids) and its text rewrites, parsed structurally |
| SPIRV-Cross WGSL (vendored) | `thirdparty/spirv-cross-wgsl/` | (vendored, 34 files, 1.8 MB) | Ludicon/spirv-cross-wgsl: SPIRV-Cross plus a WGSL backend, four local patches |
| Renderer Integration | `servers/rendering/` (42 files) | ~1,224 | API traits, format promotion, batching |
| Platform/Web | `platform/web/` (7 files) | ~388 | JS engine glue, build config |
| **Total new code** | | **~12,600** | |

---

## 2. Key Design Decisions

### 2.1 JS-First Device Initialization

**Decision**: The WebGPU device is created by JavaScript *before* WASM loads, then imported into C++ via `Module["preinitializedWebGPUDevice"]`.

**Why**: WebGPU device creation is async (Promises). Emscripten's main-thread C++ cannot yield to the event loop without Asyncify (abandoned due to binary size doubling and runtime instability). The device must be ready before WASM begins execution.

**How it works**:
1. `engine.js` calls `navigator.gpu.requestAdapter()` + `requestDevice()`
2. Device is ready before `Godot(moduleConfig)` is called
3. C++ imports the device via `WebGPU.importJsDevice(Module.preinitializedWebGPUDevice)`

With the separate render thread (the default for thread-enabled exports, `variant/separate_render_thread`; or `rendering/driver/threads/thread_model=2`) the render thread is a worker and repeats the same dance for itself: it requests its own adapter/device asynchronously on its event loop right after `Main::setup()` (the main thread waits a frame at a time — Chrome only resolves a worker's device request while the page's main thread is free), stores it in that worker's `Module.preinitializedWebGPUDevice`, and creates the context and `RenderingDevice` on its first command-queue wake. `engine.js` first probes that WebGPU works in a worker at all (`Engine.probeWebGPUWorker`); without it the engine stays on the Safe model. See `webgpu_notes/TASKS.md` §2.5.
4. From that point, all C++ code has synchronous access to the GPU device

### 2.2 Runtime SPIR-V to WGSL Conversion

**Decision**: Store raw SPIR-V in shader containers. At runtime, preprocess with `run_pipeline()`'s C++ passes and convert to WGSL with SPIRV-Cross's WGSL backend (compiled directly into the engine WASM). Cache results keyed by 64-bit hash.

**Why**:
- emdawnwebgpu only accepts WGSL, not SPIR-V
- Build-time conversion would lose runtime specialization constant support
- SPIRV-Cross's WGSL backend (Ludicon/spirv-cross-wgsl) is C++, Apache-2.0/MIT, and small (1.8 MB vendored). It replaced Tint in October 2026 (`webgpu_notes/TASKS.md` §4.36): after our passes it converts all 276 engine variants with bindings and override ids identical to Tint's, about 5-10× faster per module; its WGSL is 6-9% larger but the baked `index.shaders.pck` compresses 17% smaller (3.70 vs 4.43 MB gzip). Forward+ stayed at parity with native Vulkan (71/71), and the Razr 60 and Firefox run as fast or faster. The swap deleted ~204k lines of vendored source
- Caching prevents redundant conversions (~40ms/shader)

**Trade-off**: ~15s startup cost for first-time conversion of ~383 unique shader stages. Acceptable for initial release; pre-compilation at export time is a future optimization.

### 2.3 Push Constant Emulation via Ring Buffer

**Decision**: Emulate Vulkan push constants using a 256KB storage buffer ring with 256-byte aligned slots and dynamic offsets.

**Architecture**:
```
Ring Buffer (256KB, storage buffer on GPU):
[slot0:256B][slot1:256B][slot2:256B]...[slot1023:256B]
     ^--- push_constant_ring_offset advances per draw

CPU Shadow Buffer (256KB):
- Accumulates push constant data during command recording
- Single wgpuQueueWriteBuffer flush at submit time
- Dirty range tracking minimizes transfer size
```

**Why this design**:
- WebGPU has no push constants; emulation is required
- Storage buffer (not uniform) because push constant structs use std430 layout with arrays
- Dynamic offset allows reusing one bind group across all draws
- 256-byte alignment matches WebGPU's `minStorageBufferOffsetAlignment`
- Batched flush = one IPC crossing per frame for all push constant data
- Ring wrapping handled gracefully with pre-wrap flush

**Placement**: Group 3, binding 120 (high enough to never collide with combined-sampler split bindings which max at ~41).

### 2.4 Combined Image-Sampler Splitting

**Decision**: At the SPIR-V binary level (before conversion), split combined image-sampler variables into separate image and sampler variables with doubled binding indices.

**Why**: WebGPU/WGSL does not support combined image-samplers. Godot's GLSL shaders use `sampler2D` (combined). The SPIR-V must present separate image and sampler variables for the converter to generate valid WGSL.

**Binding convention**: Original binding N → sampler at N*2, image at N*2+1. All other bindings are also doubled to avoid collision.

**Why SPIR-V-level**: Operating at the binary level (rather than post-conversion text patching) is robust and handles edge cases — function parameters, multi-level call chains, access chain expressions.

### 2.5 Subpass Flattening

**Decision**: Each Godot render subpass maps to a separate `WGPURenderPassEncoder`.

**Why**: WebGPU has no subpass concept. Input attachments from previous subpasses are read as regular textures (same underlying WGPUTexture, bound as TextureBinding instead of RenderAttachment).

**Impact**: The Metal backend uses the same pattern. Performance impact is minimal — Godot's Mobile renderer typically uses 1-2 subpasses per render pass. Subpass-based post-processing (tone mapping) is disabled on WebGPU.

### 2.6 No-Op Barriers

**Decision**: All `command_pipeline_barrier` calls are no-ops.

**Why**: The WebGPU spec guarantees sequential execution of passes within a command encoder with implicit barriers at usage scope boundaries. There is no need for (and no API for) explicit barriers.

**Communication**: `API_TRAIT_HONORS_PIPELINE_BARRIERS = 0` tells higher-level Godot code not to waste effort inserting barriers.

### 2.7 Shadow Buffer Pattern for Async Mapping

**Decision**: All buffer reads use a CPU shadow buffer. Readbacks use a persistent staging buffer with async map callbacks and 1-frame latency.

**Why**: WebGPU buffer mapping is async-only. Godot's synchronous `buffer_map()`/`buffer_unmap()` API cannot block for a Promise. The shadow buffer allows immediate CPU access while GPU transfers happen asynchronously.

**Pattern**:
- Write path: CPU writes to shadow buffer → single `wgpuQueueWriteBuffer` flush at submit
- Read path: GPU copies to staging buffer → async `mapAsync` → data available next frame
- The 1-frame readback latency is acceptable for typical use cases (profiling, compute results, viewport capture)

### 2.8 API Traits: Capability-Driven Extension

**Decision**: Introduce `ApiTrait` enum values to the base driver interface that allow the WebGPU driver to advertise capabilities without modifying renderer logic.

| Trait | Purpose |
|-------|---------|
| `TEXTURE_GET_DATA_VIA_DRIVER` | Routes readback through async driver path |
| `TEXTURE_INITIALIZE_DIRECT_WRITE` | Skips staging for texture uploads |
| `BUFFER_CREATE_MAPPED_AT_CREATION` | Uses `mappedAtCreation` optimization |
| `STAGING_BUFFER_MAX_SIZE_MB` | Caps staging pool at 16MB |
| `SKELETON_BUFFER_DIRECT_WRITE` | Direct queue write for bone data |
| `FORCE_OMNI_DUAL_PARABOLOID` | **0 since 2026-10-17** (was: reduce shadow pass count). Omni lights use the mode they ask for, for parity with native |
| `BATCH_INSTANCE_DRAWS` | Instance batching for shadows + color |
| `FIRST_INSTANCE_INDEX` | Eliminates per-draw push constant overhead |

All traits default to 0 in the base class — non-WebGPU backends are unaffected.

---

## 3. How It Compares to Vulkan/Metal Backends

| Aspect | Vulkan | Metal | WebGPU |
|--------|--------|-------|--------|
| Object model | Heap alloc, pointer-as-ID | Same | Same |
| Push constants | Native | Emulated (ring buffer) | Emulated (ring buffer) |
| Subpasses | Native | Flattened | Flattened |
| Barriers | Explicit, complex | Tracked implicitly | No-op |
| Shader format | SPIR-V native | SPIR-V → MSL (SPIRV-Cross) | SPIR-V → WGSL (SPIRV-Cross) |
| Combined samplers | Native | Split at shader level | Split at SPIR-V level |
| Multi-draw indirect | Native | Emulated (loop) | Emulated (loop) |
| Buffer mapping | Synchronous | Synchronous | Shadow buffer (async) |
| Pipeline cache | Explicit API | Implicit | No API (browser handles) |
| Queue count | Multiple | Multiple | Single |
| Secondary cmd buffers | Native | Native | Not available |

### Key Divergences

1. **BGL Adaptation**: WebGPU requires bind group layouts to match exactly. The driver maintains a rebind cache that re-creates bind groups when shader BGL expectations differ from the original.

2. **Format Promotion**: WebGPU has limited storage texture format support. R8/RG8/R16/RG16 are promoted to 32-bit equivalents at both the texture and WGSL level.

3. **Strip Topology Dual Pipelines**: WebGPU bakes `stripIndexFormat` into pipeline state. The driver creates both Uint16 and Uint32 variants, selecting at draw time.

4. **Encoder Splitting**: When a texture has both TextureBinding and RenderAttachment usage, the driver proactively splits the command encoder to prevent sync scope violations.

---

## 4. The Shader Pipeline

### Complete Flow

```
GLSL (Godot shaders, authored or built-in)
    | glslang (at Godot compile time)
SPIR-V 1.3 bytecode (stored in shader container)
    | C++ driver: specialization constant patching (if needed)
    | 64-bit hash cache lookup (hit -> return cached WGSL)
    | spirv_preprocess::run_pipeline() (full order: TECHNICAL_REFERENCE.md 4.1):
        - inline opaque functions; freeze spec constants unless all can be overrides
        - SPIR-V 1.4+/1.6 rewrites, push constants -> storage buffer (group 3, binding 120)
        - split combined samplers (texture 2b, sampler 2b+1), depth=2 fix, Y-flip
        - strip what the converter rejects (decorations, barriers, operand masks,
          helper invocations, subgroup ops, Inf/NaN literals, initialized local arrays)
        - IR passes: lower isnan/isinf/textureQueryLod/PointCoord, apply sampler LOD bias,
          expand texture arrays (budgeted per stage)
        - collapse remaining arrays, read-only SSBO inference, dead-resource removal
    | SPIRV-Cross WGSL backend (SPIR-V -> WGSL, compiled into engine WASM)
    | Return WGSL string
    | C++ WGSL post-processing:
        - Format remapping (r8/rg8/r16/rg16 -> 32-bit equivalents)
        - read_write storage demote for vertex/fragment stages
        - hex float fix, strip f16, add diagnostics
    | wgpuDeviceCreateShaderModule
```

### SPIRV-Cross Patches (4 patches, in `spirv_wgsl.*` and one in `spirv_glsl.*`, never upstreamed)

1. **`0001` workgroup uniform load**: the GLSL broadcast idiom (barrier, load of a workgroup variable, barrier) becomes `workgroupUniformLoad(&var)`, so an early return on it passes WGSL's barrier-uniformity analysis (FSR2's luminance pyramid)
2. **`0006` 16-bit normalized storage formats**: `R16`/`Rg16`/`Rgba16` and their Snorm forms map to `r16unorm` ... `rgba16snorm` (`CanvasSdfShaderRD`)
3. **`0015` uniform compute builtins as parameters**: `workgroup_id` / `num_workgroups` reach the inner entry point as parameters instead of `var<private>` reads, which WGSL treats as non-uniform (screen-space contact shadows)
4. **`0019` no `for` statements**: every loop is emitted as `loop { if (!c) { break; } ... }`; Mali-G615 / Chrome ran `for (var i = -3; i <= 3; i++)` zero times (volumetric fog on the Razr 60)

The history: naga (42 patched Rust files) until May 2026, then Tint (6 patches over 8 files, ~9 MB vendored) until October 2026, then SPIRV-Cross. The list and rationale live in `thirdparty/README.md`.

---

## 5. The Performance Architecture

### The Core Problem

WebGPU on the web runs inside a browser sandbox. Every GPU API call crosses an IPC boundary: WASM → JS (V8 context switch) → GPU process (Mojo IPC). A draw call costs ~0.2-0.5 microseconds vs ~5ns on native. This 40-250x overhead makes Godot's renderer (designed around "commands are free") IPC-bound, not GPU-bound.

### The Strategy

**Reduce the number of IPC messages** rather than optimizing GPU utilization:

```
Layer 0: Staging fixes + Direct Write     -> eliminate loading/per-frame overhead
Layer 1: Shadow Pass Merging + DP mode    -> reduce render pass count (196 -> 4)
Layer 2: Instance Batching (shadow+color) -> merge N draws into 1
Layer 3: firstInstance + PC dedup         -> eliminate per-draw SetBindGroup
```

### The Journey to Parity

| Phase | Frame Time | vs Native |
|-------|-----------|-----------|
| Baseline (no optimization) | 133 ms | 3.25x slower |
| +Staging fixes | ~120 ms | ~2.9x |
| +Dual-paraboloid + pass merging | 76 ms | ~1.86x |
| +Shadow instance batching | 57 ms | ~1.39x |
| +Skeleton atlas | ~48 ms | ~1.17x |
| +firstInstance dedup | ~46 ms | ~1.12x |
| +Color pass batching | ~36 ms | **Parity** |

---

## 6. Platform Integration

### Initialization Sequence

```
[Browser]                  [Engine JS]                    [WASM/C++]
    |                          |                              |
    | Engine.startGame()       |                              |
    |->                        |                              |
    |                          |                              |
    |  requestWebGPUDevice()   |   device + features          |
    |<-                        |                              |
    |                          |                              |
    |                          | Godot(moduleConfig)          |
    |                          |->                            |
    |                          |   DisplayServerWeb           |
    |                          |   RenderingContextWebGPU     |
    |                          |   importJsDevice()           |
    |                          |   RenderingDevice::initialize|
    |                          |   First frame                |
```

### Audio

A web export plays sound two ways, chosen per player by `playback_type` (default from the project setting
`audio/general/default_playback_type.web`, which is **Sample**):

```
Stream:  AudioStreamPlayback -> AudioServer::_mix_step() (buses, effects) -> int32 ring buffer
             -> AudioWorklet (audio thread: threads=yes; main thread drives it: threads=no) -> speakers
Sample:  AudioStream rendered once at the mix rate -> AudioBuffer (shared by every play)
             -> AudioBufferSourceNode -> fader -> [3D high shelf] -> mixer per bus (one gain per output channel)
             -> bus volume -> bus mute/solo -> send to the next bus or the destination
```

Stream is the native mixer, unchanged. Sample (`GodotSampleDriver` in `platform/web/js/libs/library_godot_audio.js`,
fed by `AudioDriverWeb` in `platform/web/audio_driver_web.cpp`) exists so that a single-threaded export does not mix
on the main thread, and it re-implements `AudioServer`'s behavior on a Web Audio graph: per-channel volume vectors,
volume ramps and fade-outs over one 512-frame mix block, loop modes (ping-pong and backward unrolled into the buffer),
the bus routing rules (sends to earlier buses go to Master, solo chains, mute), the 3D attenuation filter, and
playback positions computed from the context clock. The C++ side hands JS small integer handles and skips calls whose
values did not change, so a playing 3D sound costs nothing per frame unless it moves. Sample playback cannot run bus
effects or stream (it decodes a sound whole on its first play); both are warned about at export and at runtime.
Parity is tested by `webgpu_tests/web_audio` (and the smoketest's `audio_parity` scene); history and the deferred
options are `webgpu_notes/TASKS.md` §4.30 and §2.32.

### Build System

- SCons with `webgpu=yes` flag, `--use-port=emdawnwebgpu` for Emscripten
- SPIRV-Cross's WGSL backend and SPIRV-Tools compiled directly into the engine WASM (no separate sidecar binary); the SPIRV-Cross TUs alone get `-fwasm-exceptions`
- No Rust toolchain needed — all C++ dependencies built with SCons
- Standard Godot build dependencies only (SCons, Python, C++ compiler, Emscripten)

---

## 7. Limitations vs Vulkan Backend

### Not Supported in WebGPU Spec
- Multiview (no VR stereo rendering)
- Variable Rate Shading
- Tessellation / Geometry Shaders
- Subgroups / Wave operations
- Buffer Device Address / Bindless
- Hardware point/line size control (the scene shaders emulate point size by drawing each point as a quad, `sc_emulate_point_size`)

### Implemented with Trade-offs
- Omni shadows: until 2026-10-17 forced to dual-paraboloid (the 43% gain was measured together with shadow-pass merging). Now each light's own `omni_shadow_mode` is honored -- cube by default, as on native; set dual-paraboloid per light to get the cheaper shadows back
- Texture readback: async with 1-frame delay
- Storage textures: limited formats requiring promotion
- Multi-draw indirect: loop emulation
- Indirect draw count: always uses max count
- Texture arrays: expanded to one binding per element (multiple lightmaps, VoxelGI probes, particle colliders work). Arrays past a stage's texture budget (Chrome reports exactly the 48 sampled textures Forward+ needs) stay collapsed to element 0, with a one-time warning
- Sampler LOD bias: applied in the shader from a uniform, implicit-LOD samples only
- Constant-alpha blend factors: emulated through the single RGBA blend constant

### Different from stock Godot's web export
- Occlusion culling: stock web templates leave out the raycast module (Embree), so nothing is culled; this fork's include it by default (+0.23 MiB gzipped wasm) and fix the culler's scene commit in `threads=no` builds. The Web export option `rendering/occlusion_culling` turns it off per export (`webgpu_notes/TASKS.md` §4.38)

### Stubs / TODOs
- `command_resolve_texture`: unimplemented (MSAA resolve via render pass only)
- `command_render_clear_attachments`: mid-pass clears silently ignored
- `get_total_memory_used`: returns 0
- No pipeline caching API (browser handles internally)

---

## 8. Upstream Acceptance Considerations

### What Would Need to Change for Mainline Godot

1. **Base driver interface**: 10 new ApiTrait enums and ~9 new virtual methods affect all backends (have safe no-op defaults, but need review)
2. **Vendored SPIRV-Cross WGSL + SPIRV-Tools**: ~9 MB of vendored source (1.8 + 6.9 MB), and a second SPIRV-Cross beside Metal's (its own namespace). Upstream would want to evaluate vendoring strategy
3. **SPIRV-Cross patches**: 4 small local patches to the WGSL backend; by decision they are not offered upstream
4. **Test infrastructure**: Need headless Chrome/Firefox WebGPU for CI
5. **Shared constants**: Magic numbers (binding 120, ring size 256KB) should be in one shared header

### What Aligns Well with Godot Architecture
- Follows RenderingDevice abstraction correctly
- Uses established heap-alloc + pointer-as-ID pattern
- Properly isolated behind `WEBGPU_ENABLED` define
- Respects export template model
- Mobile renderer auto-selection based on device limits is correct

---

## 9. Architecture Assessment

### Strengths
- **Correct abstractions**: Successfully bridges Godot's Vulkan-centric interface to WebGPU's different model
- **Performance-aware design**: IPC reduction is baked into the architecture, not bolted on
- **Comprehensive documentation**: Extensive inline comments explain "why" not just "what"
- **Graceful degradation**: Missing features detected at init and handled without crashes
- **Consistent patterns**: Object lifecycle, error handling, and async safety are uniform throughout

### Concerns
- **SPIRV-Cross patch maintenance**: 4 local patches (`spirv_wgsl.*`, one touching `spirv_glsl.*`), 25-125 lines each; must be reapplied when the fork is updated. The backend has one upstream author and started in October 2026
- **Vendored dependency size**: SPIRV-Cross WGSL (~1.8 MB) + SPIRV-Tools (~6.9 MB) = ~9 MB of vendored source (Tint's 9.1 MB is gone)
- **Single developer knowledge**: No co-authors visible in commit history (bus factor risk)

### Overall Grade: B+

The architecture is sound, pragmatic, and production-ready for its target use case. It correctly identifies and addresses the 9 major gaps between WebGPU and Vulkan with well-designed emulation. The main risks are maintainability and upstream acceptability — both addressable with incremental refactoring that doesn't change the fundamental design.
