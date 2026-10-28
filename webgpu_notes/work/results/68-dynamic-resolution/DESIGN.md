# Dynamic 3D resolution: design (task 68)

Status: design for the user's decision. No engine code was changed in this round. The numbers below
come from a throwaway bench (`proto/` beside this file: `main.gd`, `heavy.gdshader`, `run_web.mjs`) run on
the RTX 4080 SUPER, 2560x1440 at 165 Hz, Chrome 156 (Vulkan) and Playwright's Firefox, and natively
on Vulkan, with the lead's `bin/` editor and nothreads `template_release` of `467336d39` (copied to scratch
before use). Raw results: `data/` beside this file (one JSON line per bench event).

## 1. What exists today

**Changing `scaling_3d_scale` is a full reconfigure.** `RendererViewport::viewport_set_scaling_3d_scale()`
calls `_configure_3d_render_buffers()`, which calls `RenderSceneBuffersRD::configure()`, whose first act
is `cleanup()`: every internal-size texture is freed and recreated, and every `RenderBufferCustomDataRD`
gets `free_data()` + `configure()`. Concretely, on every scale change:

| state | where | what a change does to it today |
|---|---|---|
| colour/depth/MSAA/velocity, SSAO/SSIL/SSR/SSCS buffers, cluster builder | `RenderSceneBuffersRD`, `RenderBufferDataForwardClustered::free_data()` | freed and reallocated at the new size |
| **FSR 2 context** | `RenderBufferDataForwardClustered::fsr2_context` | `memdelete`d; `ensure_fsr2()` creates a new one next frame with `maxRenderSize` = the *new* internal size. New context = `firstExecution` = accumulation reset (`ffx_fsr2.cpp:822`): the upscaled history (display size) is thrown away |
| TAA history | named texture `taa/history` | freed; `TAA::process()` reseeds it from the current frame (no flash, but the frame and the next few are un-accumulated: aliasing/shimmer returns until it re-converges) |
| **auto-exposure luminance** | `Luminance::LuminanceBuffers::current` | freed and recreated **cleared to 0**; `luminance_reduce.glsl` then adapts from `prev_lum = 0` (clamped to the min sensitivity) at `auto_exposure_speed` — an exposure pump on every change. `set_immediate` is not raised because the camera attributes' version did not change |
| SDFGI / VoxelGI / volumetric fog | per-viewport, but fog's froxel buffers follow the internal size | fog: reallocated (temporal reprojection restarts) |
| samplers (mip bias = log2(scale)) | `update_samplers()` | recreated (cheap) |

FSR 2 itself is built for dynamic resolution: a context made with `maxRenderSize` accepts any
`renderSize` up to it per dispatch (`ffx_fsr2.cpp:263`, `constants.renderSize` vs `maxRenderSize`), and
its history is at display size. Godot simply never uses that: it destroys the context instead.

**GPU time.** `RenderingServer.viewport_set_measure_render_time()` / `viewport_get_measured_render_time_gpu()`
bracket a viewport's draw with `capture_timestamp("vp_begin_<id>")`/`("vp_end_<id>")`; the results are read
when the frame slot is reused, i.e. 2-3 frames late. Vulkan, D3D12 and Metal implement RD timestamps;
GLES3 only over desktop GL (`glQueryCounter`, not GLES/WebGL). **On WebGPU the driver has timestamp
readback switched off** (`rendering_device_driver_webgpu.cpp:1341`, "mapAsync can get stuck pending"),
so the API returns 0. Two more problems in that driver path: it writes timestamps with
`wgpuCommandEncoderWriteTimestamp`, i.e. `GPUCommandEncoder.writeTimestamp()`, which is not in the
WebGPU spec (see §3 for which browsers have it), and it maps the readback buffer every frame.

**camera-sim's Auto** (`scripts/main.gd` 1027-1215) resizes its SubViewport through a candidate list
(Match screen, 1440p, 1080p, 900p, 720p), settling 1 s + 10 frames per change (3 s after startup),
passing a size when the median of 60 frames is <= 1/55 s and at most a quarter miss 25 ms, failing early,
and saving the most pixels that passed per machine/browser/window mode in `user://display.cfg`.
Its project uses TAA and no engine auto exposure.

## 2. Measurements

The bench (`proto/main.gd`) renders a 1920x1080 Forward+ scene (SSAO, SSR, glow, 32 omni lights, a shadowed sun)
whose per-pixel cost is set by `iters`, and can add a CPU busy-wait per frame (`cpu_ms`) that resolution cannot
touch. On the web, `proto/run_web.mjs` gives the page a GPU-time source the engine does not have today (every command
encoder bracketed by timestamp writes, read back every 200 ms) and the submit-to-`onSubmittedWorkDone` latency. Every
browser run asserted its adapter (Chrome: nvidia/lovelace, not fallback, 48 sampled textures; Firefox: not fallback).
Native timing runs were windowed on the real display: under Xvfb the native frame is present-bound at ~50 ms
(GPU 13 ms), which is useless for a sweep but turned out to be a good CPU-bound case (§2.4).

### 2.1 Does a lower scale lower the frame? (sweep, `iters=3000`, bilinear)

| scale | native GPU ms | native frame ms | Chrome GPU ms | Chrome frame ms | Firefox GPU ms | Firefox frame ms |
|---|---|---|---|---|---|---|
| 1.00 | 13.9 | 13.9 | 16.4 | 20.4 | 14.5 | 20.8 |
| 0.85 | 11.0 | 11.1 | 14.0 | 18.1 | 11.2 | 18.1 |
| 0.70 | 8.8 | 9.1 | 11.1 | 15.3 | 8.9 | 15.0 |
| 0.60 | 7.7 | 8.2 | 9.7 | 14.3 | 7.1 | 13.0 |
| 0.50 | 6.2 | 6.7 | 8.4 | 8.3 | 5.2 | 10.9 |

- **GPU time is `a + b * scale^2`, with a real fixed part**: native a = 3.7 ms, b = 10.2 ms (fits every point within
  0.15 ms); Chrome a = 5.7, b = 10.7; Firefox a = 2.1, b = 12.4. The fixed part (shadows, sky, glow at display size,
  the 2D pass, the upscale) is 14-35 % of the full-scale frame here. A controller that assumes pure `scale^2` predicts
  too much gain from a step down and too little cost from a step up; it must estimate `a` (§3.2).
- **Web frame time follows GPU time, quantized** (Chrome rAF at 165 Hz: multiples of 6.06 ms with phase noise; the
  timestamp hook itself added ~4 ms to Chrome frames, so Chrome's frame column is from the hooked run). A
  GPU-bound page's frame time does carry the GPU signal, which is why a frame-time fallback can work at all.
- **The web GPU queue is deep.** Submit-to-done latency, GPU-bound (scale 0.6-1.0): Chrome 113-156 ms (7-8 frames), Firefox 36-77 ms
  (3-4 frames); CPU-bound: ~2 frames in both (50 ms at a 25 ms frame). Anything the controller changes shows in
  frame time only after the queue drains: settle at least ~0.25 s on the web.
- **The engine reports no GPU time on WebGPU** (`viewport_get_measured_render_time_gpu()` = 0 throughout), as §1 said.
  Both browsers' adapters do expose `timestamp-query` (Chrome 156 and Firefox both listed it), and
  `GPUCommandEncoder.writeTimestamp` exists in Chrome only (Firefox: `undefined`), so the driver's current call would
  fail on Firefox even once enabled. Safari was not measured (no Safari run in this round).

### 2.2 What one scale change costs (hitch, 1.0 <-> 0.75, 10 changes each)

| | setter (CPU, ms) | first frame after | worst frame in the 6 after |
|---|---|---|---|
| native, bilinear | 0.7-2.0 | 9-14 ms (base 14.6) | 19.8 ms |
| native, FSR 2 | 1.3-2.3 (context destroyed + recreated) | 9-15 ms (base 14.7) | 24.6 ms |
| Chrome, bilinear | 0.4-0.7 | render-server CPU 1.3-6.5 ms vs ~1 | 68 ms (3 of 10 changes had a 26-69 ms frame) |
| Chrome, FSR 2 | 0.7-1.4 | | 65 ms (5 of 10 had a frame over 30 ms) |
| Chrome, light scene (vsync-bound) | 0.3-0.5 | 6.1 ms | 8.7 ms (no visible hitch) |
| Firefox, bilinear | 0.4-0.5 | | 63 ms (2 of 10 had a frame over 40 ms; base 23 ms) |

A change costs well under a frame of CPU, plus on the web an occasional single 25-70 ms frame (reallocating the
internal-size textures and recreating their bind groups while 7 frames are in flight). That is acceptable for a
controller that changes scale a couple of times a minute; it rules out one that changes every frame, and it is why
§4 proposes keeping the allocations where it can.

### 2.3 What the picture does across a change (native capture, 1.0 -> 0.6, static camera)

Mean luminance (L) and mean absolute change against the previous frame (d), per frame relative to the change:

| frame | FSR 2: L / d | TAA: L / d | bilinear + auto exposure: L / d | FSR 2 + auto exposure: L / d |
|---|---|---|---|---|
| -1 | 0.741 / 0.010 | 0.742 / 0.0015 | 0.741 / 0 | 0.741 / 0.016 |
| 0 | 0.737 / **0.037** | 0.741 / **0.017** | **0.991** / **0.253** | **0.966** / **0.229** |
| 1 | 0.738 / **0.046** | 0.743 / 0.007 | 0.978 / 0.012 | 0.902 / 0.073 |
| 3 | 0.739 / 0.027 | 0.744 / 0.002 | 0.873 / 0.073 | 0.803 / 0.047 |
| 6 | 0.741 / 0.010 | 0.744 / 0.0015 | 0.774 / 0.018 | 0.758 / 0.015 |
| 12 | 0.741 / 0.002 | 0.743 / 0.001 | 0.744 / 0.001 | 0.742 / 0.002 |

- **Auto exposure flashes on every change**: +34 % luminance on the change frame (the reset luminance reads as "very
  dark scene"), decaying over ~10 frames. This is the most visible artifact and the cheapest to fix (§4.3).
- **FSR 2 resets**: frame-to-frame change jumps 4-5x for two frames and takes ~8 frames to re-converge (the
  accumulated history is thrown away with the context). Visible as a shimmer/softness pulse, not a flash.
- **TAA** reseeds from the current frame: one frame of 11x the usual change, back to normal in two frames. Mild.
- Bilinear alone: nothing beyond the resolution change itself.

### 2.4 The controller, prototyped (60 fps target, 0.5-1.0, step 0.05)

| run | changes | settled changes/min | where it settled |
|---|---|---|---|
| native, GPU time, load drops at 40 s | 6 | 1.3 | 0.85 (frame 13.3 ms) -> 1.0 within 1.9 s of the drop |
| native, frame time only | 6 | 2.7 | 0.90 -> 1.0 |
| Chrome, GPU time (page hook) | 4 | 1.3 | 0.90 -> 1.0 within 1.9 s |
| Chrome, frame time only | 10 | 5.3 | oscillates 0.85 <-> 0.95 before the drop; 1.0 after |
| Chrome, FSR 2, frame time only | 7 | 8.0 | oscillates 0.85-1.0 |
| Firefox, frame time only | 6 | 2.7 | 0.85 -> 1.0 |
| **CPU-bound** (24 ms busy-wait, light GPU), GPU time: native / Chrome | **0 / 0** | 0 | 1.0, reported CPU-bound every 5 s |
| **CPU-bound**, frame time only: native / Chrome / Firefox | 8 / 8 / 5 | 5.3 | 1.0; each probe down (to 0.75) reverted after 1.4 s, backoff 5, 10, 20 s (the Firefox page stopped responding at 23 s and the run timed out; its first two probes and reverts are the same as Chrome's) |
| present-bound (native under Xvfb: frame 50 ms, GPU 8-17 ms), GPU time | 1 | 0 | one step at exactly the threshold, then CPU-bound |

What the runs taught:
- **With GPU time the problem is easy**: lower when GPU time is over the budget, raise when the predicted GPU time
  at the next step fits under 80 % of it; CPU-bound is simply "frames miss while the GPU has room", detected without
  touching the scale. Changes per minute settled: 1.3.
- **Frame time alone oscillates when probing up.** Under vsync the frame time is quantized (165 Hz: 12.1 or 18.2 ms,
  nothing between), so "12.5 ms" says "fits", the probe goes up a step, misses, comes back: 5-8 changes/min in
  Chrome. On a 60 Hz display it is worse: every frame is 16.7 or 33.3 ms, so the fallback learns only pass/fail per
  scale, exactly what camera-sim's Auto learns. The fallback therefore needs camera-sim's memory: remember the
  lowest scale that failed and back off probing it exponentially (§3.3).
- **Frame-time CPU-bound detection works, at a price**: step down a large step, see whether the frame improved by a
  quarter of what the `a + b*s^2` model predicts, revert if not. Each probe costs ~1.4 s at a lower resolution;
  with exponential backoff that is 4 probes in the first minute, then one every minute or two.
- **A small verification step lies.** One early Chrome probe stepped 1.0 -> 0.95 on a GPU-bound frame, predicted a
  1.8 ms gain, saw 0.4 ms (below one vsync quantum) and wrongly called it CPU-bound. The verifying step must be big
  enough that its predicted gain exceeds one refresh interval or 15 % of the frame, whichever is larger.
- **Native frame time is not CPU time.** Natively the render server's CPU time (`frame_pre_draw` -> `frame_post_draw`)
  tracked GPU time almost exactly (13.4 vs 13.9 ms at 1.0): it blocks on the GPU. A "CPU time" signal cannot be
  taken from it; only GPU time or the step-down verification separates the two.

### 2.5 camera-sim (the CPU-bound reference)

Its current web export (`builds/`, copied to scratch, Chrome, canvas 1656x968 since the export keeps its project
window size, TASKS.md §3 trap 98): after Start, frame time median 12-30 ms (p90 21-50 ms) while the GPU is busy
2.4-5.8 ms per frame, submit-to-done ~2 frames. Its own Auto had settled at **"Auto (700p)", the bottom of its list**,
on an RTX 4080 SUPER: each step down "failed" because resolution was never the limit. An engine controller with GPU
time would have stayed at full resolution and reported CPU-bound; with frame time only it would have probed down
and reverted. (`gpu_time.mjs` itself cannot measure this export: it never clicks camera-sim's Start button and waits
for frames that never come, so the measurement used the bench's hook with a click, `data/camsim_gpu.json`.)

## 3. The control loop

Runs per viewport, on the rendering server, once per drawn frame, before the viewport's render buffers are
configured. All numbers are the proposed defaults (§6) and what the prototype ran with.

### 3.1 Inputs, in order of preference

1. **GPU time of this viewport** (`vp_begin`/`vp_end` timestamps, already captured when measurement is on; the
   controller turns measurement on for its viewport). Vulkan, D3D12, Metal today; desktop GL (Compatibility) via
   `glQueryCounter`; **WebGPU after the driver fix in §7.1**. Arrives 2-3 frames late natively, ~0.3-0.5 s on the web.
2. **Frame time** (`Engine` frame delta) where no GPU time exists: WebGL/GLES, WebGPU without `timestamp-query`,
   and any driver whose timestamps read 0. Global, not per viewport (§3.5).
3. Not used: submit-to-done latency. GPU-bound frames had 3-8 frames in flight against ~2 when CPU-bound, so it is a
   usable hint in Chrome, but Firefox's margin (3.5 vs 2) is thin and it depends on the browser's queueing, which is
   not specified. Kept as a possible later refinement of the fallback (question Q6).

### 3.2 GPU-time path

- Budget `B = 1000 / target_fps` ms. Window: 30 accepted frames, median.
- Frames after a change are discarded for `settle = max(0.25 s, 8 frames)` **plus** the timestamp latency (the
  results that describe the old scale must not be read as the new one). The first 3 s after the controller is
  enabled or the scene changes are ignored (shader compiles, loading). Frames over `4 * B` are hitches, not load,
  and are skipped.
- Model: `gpu(s) = a + b*s^2`. Start with `a = 0.25 * gpu`; after each change, re-fit `a, b` from the last two
  settled (scale, GPU ms) pairs when they differ by at least 0.1 in scale, clamped to `0 <= a <= 0.6 * gpu`.
- **Down** when the median GPU time is over `0.95 * B`: jump straight to the scale the model says gives `0.85 * B`,
  quantized to the step, not one step at a time (the native run went 1.0 -> 0.8 in two decisions, not five).
- **Up** one step when the model says the next step stays under `0.80 * B`. The gap between 0.80 and 0.95 is the
  hysteresis band; nothing changes inside it.
- **CPU-bound**: the frame misses (median frame over `1.05 * B`, or a quarter of the window over `1.5 * B`) while GPU
  time is under `0.75 * B`. The controller does not lower the scale, reports `CPU_LIMITED` (§5) and checks again
  every 5 s. It still raises the scale if GPU time allows (resolution is free in a CPU-bound frame).

### 3.3 Frame-time path (no GPU time)

- Same window, settle, startup and hitch rules.
- **Down** when the window misses (as above): step to the model's scale for `0.9 * B` but at least a *verifying step*
  (`step >= 0.15`, or larger if the predicted gain is under one refresh interval). Then verify: if the frame did not
  improve by at least 25 % of the predicted gain, revert, report `CPU_LIMITED`, and do not try again for 5 s, doubling
  up to 60 s on each consecutive failure.
- **Up** only when the window is comfortably inside the budget (median under `0.8 * B` with no missed frame), one
  step at a time, and never to a scale that failed within the last `probe_backoff` (10 s, doubling to 120 s per
  failure at that scale). A failed probe reverts at once. This is camera-sim's "remember what failed", kept per run.
- With the display's refresh at or below the target (60 Hz display, 60 fps target), headroom is invisible: the path
  can only hold a scale that passes and probe up rarely. Documented as a limit of the fallback, not hidden.

### 3.4 Quantization and stability

Scales are quantized to `step` (default 0.05) and clamped to `[min_scale, max_scale]`. A scale is only ever set
after the settle period, so the worst case is about two changes per second while converging and, settled, the
measured 1.3 changes per minute with GPU time (target: under 2 per minute).

### 3.5 Several dynamic viewports

GPU time is per viewport, so each controller works alone. Frame time is global: with the fallback, controllers
would all see the same miss and all step down. Proposal: in the frame-time path only one viewport changes per settle
period, the one with the most internal pixels first (question Q5).

## 4. FSR 2, TAA and auto exposure across a change

Today every change is a full reconfigure (§1) and §2.3 shows what that costs on screen. The proposal keeps the
reallocation (it is cheap, §2.2) but stops throwing away the temporal state:

### 4.1 FSR 2: one context for the viewport's life

Create the context with `maxRenderSize = target size * max_scale` (what FSR 2 is designed for) and keep it across
scale changes: `RenderBufferDataForwardClustered::configure()` frees it only when the target size, `max_scale`, HDR
format or view count change, not when only the internal size does. Each dispatch already passes `renderSize` and
`motionVectorScale` from the current internal size (`fsr2.cpp:1057-1061`), and FSR 2's history is at display size,
so a scale change is just a different `renderSize` that frame. `reset` stays false. Jitter: the phase count follows
the render/display ratio; Godot's jitter sequence must use the current ratio (check
`RendererSceneRenderRD`'s jitter phase count when building). Cost: FSR 2's internal-size intermediates are allocated
at the max size, ~the same memory as running at max_scale.

### 4.2 TAA: resample the history instead of dropping it

TAA's history is at internal size. On a change, before the old `taa/history` is freed, blit it bilinearly into the
new-size texture (one draw); `prev_velocity` is cleared to zero (one frame without reprojection velocity, harmless
for a static or slow camera). Simpler alternative if that is not worth it: keep today's reseed, which §2.3 shows is
a one- or two-frame artifact (question Q3).

### 4.3 Auto exposure: keep the luminance

The final 1x1 luminance (`LuminanceBuffers::current`) does not depend on the internal size. Carry it across
`configure()` (re-create only the size-dependent reduce chain) so the adaptation continues from the last value
instead of 0. Removes the +34 % flash for every change, dynamic or manual. This one is worth doing even if dynamic
resolution were dropped: any script changing `scaling_3d_scale` today flashes.

### 4.4 Other internal-size state

Volumetric fog's froxels follow the internal size and restart temporal reprojection; SSR/SSAO/SSIL have no
cross-frame state worth keeping beyond one frame. Left as today (question Q4 asks whether fog should be pinned to
the max size).

### 4.5 Not proposed: render a sub-rectangle of max-size buffers

The usual console approach (allocate once at max scale, render into a viewport rectangle) removes the reallocation
hitch completely, but every RD effect in Forward+ reads its inputs by full-texture UV and would need a UV scale and
clamp: a renderer-wide change with a long tail of edge bleeding bugs. With a measured change cost under a frame of CPU
and changes a couple of times a minute, not worth it now. Revisit if the web's occasional 25-70 ms change frame
proves visible in practice.

## 5. API

On `Viewport` (and `RenderingServer.viewport_set_scaling_3d_dynamic*`), with project settings under
`rendering/scaling_3d/` for the root viewport as for `scaling_3d_scale`:

| member | type, default | meaning |
|---|---|---|
| `scaling_3d_dynamic` | bool, `false` | on/off. Off = exactly today's behavior: no controller, no extra timestamps |
| `scaling_3d_dynamic_target_fps` | float, `60.0` | the frame rate to hold |
| `scaling_3d_dynamic_min_scale` | float, `0.5` | lower bound |
| `scaling_3d_dynamic_max_scale` | float, `1.0` | upper bound (> 1.0 allowed with Bilinear: supersampling when there is room; clamped to 1.0 with FSR 2, which only upscales) |
| `scaling_3d_dynamic_step` | float, `0.05` | quantization |
| `get_scaling_3d_effective_scale()` | float | the scale in use (equals `scaling_3d_scale` when off) |
| `get_scaling_3d_dynamic_state()` | enum | `OFF`, `STARTING`, `ADJUSTING`, `STABLE`, `CPU_LIMITED`, `AT_MIN_SCALE` |
| `get_scaling_3d_dynamic_source()` | enum | `GPU_TIME` or `FRAME_TIME`: which path is running |
| signal `scaling_3d_effective_scale_changed(scale)` | | emitted on the main thread after a change |

`scaling_3d_scale` stays the user's value and, when dynamic is on, is the **starting scale** (so a project that
saved its settled scale sets it back through the existing property, §8). The controller never writes it. The state
enum is what lets a UI say "Auto (1080p)" or "limited by CPU".

The controller lives in `RendererViewport` (it already owns the viewport's timestamps and the scale), and the scene
side reads the effective scale through `RenderingServer` when it emits the signal (polled once per frame from
`Viewport` only while dynamic is on).

## 6. Defaults

| setting | default | from |
|---|---|---|
| off | `false` | spec; "off: bit-identical" |
| target | 60 fps | spec, camera-sim |
| min / max / step | 0.5 / 1.0 / 0.05 | spec; 0.5 is where Chrome's fixed part already dominated (§2.1) |
| window | 30 frames, median | prototype (camera-sim used 60; 30 halves reaction time without more oscillation) |
| settle | max(0.25 s, 8 frames) + timestamp latency | the web queue depth (§2.1) |
| startup ignore | 3 s | camera-sim |
| hitch | frame > 4 x budget skipped | prototype |
| down / up thresholds | GPU > 0.95 B -> aim 0.85 B; up if next step < 0.80 B | prototype, 1.3 changes/min |
| missed frame | > 1.5 x budget (25 ms at 60 fps, camera-sim's) | camera-sim |
| CPU-bound | misses with GPU < 0.75 B; fallback: < 25 % of predicted gain | prototype |
| backoff | CPU probe 5 s doubling to 60 s; failed up-probe 10 s doubling to 120 s | prototype |

## 7. Per-renderer and per-platform behavior

### 7.1 WebGPU: GPU time needs a driver fix first

The design is only "GPU time where the browser allows" once the driver's timestamps work. Required, in
`rendering_device_driver_webgpu.cpp`:
- write timestamps through `timestampWrites` on the first and last render/compute pass of the range (the spec way;
  `writeTimestamp()` on an encoder is Chrome-only and absent in Firefox), or an empty compute pass with
  `timestampWrites` when the range has no pass;
- read back through a small ring of `MAP_READ` buffers polled with the async-readback status task 66 added
  (`RDD::buffer_get_readback_status`), never mapping a buffer that is still pending and never every frame; that is
  what the disabled code at ~1341 got wrong;
- the bench's hook cost Chrome ~3-4 ms per frame because it wrapped *every* encoder in two extra passes and resolved
  every frame; the engine version needs two timestamps per measured viewport and one resolve per frame.
Without `timestamp-query` (Safari unknown, older Chrome/Firefox, or a device created without it) the frame-time path
runs. Chrome quantizes timestamps to 100 us without developer flags, which is irrelevant at a 16.7 ms budget.

### 7.2 Renderers

| | dynamic scale | upscalers | GPU time |
|---|---|---|---|
| Forward+ (native, WebGPU) | yes | Bilinear, FSR 2 (kept context, §4.1), TAA (§4.2); FSR 1 natively only | native: yes; WebGPU: after §7.1 |
| Mobile | yes | Bilinear, FSR 1 (native) | native: yes; WebGPU: Mobile runs only on a software adapter here (CLAUDE.md) |
| Compatibility | yes, bilinear only (what `scaling_3d` supports there) | - | desktop GL: yes; WebGL/GLES: frame time |

- **FSR 1 on WebGPU**: renders black today, so the setter refuses dynamic + FSR 1 on WebGPU with a warning (or falls
  back to bilinear: question Q2).
- **XR**: off in the first build (XR runtimes have their own frame pacing and resolution rules); the setter warns.

## 8. Persisting a result (the project's job)

Read `get_scaling_3d_effective_scale()` when the state is `STABLE` (or on quit), save it per machine/browser/window
mode in `user://`, and on the next start set `scaling_3d_scale` to it before enabling `scaling_3d_dynamic`: the
controller starts there and only adjusts. Picking "Auto" again = clear the saved value.

## 9. Costs

- **Off**: nothing. No controller, no timestamp writes beyond what `viewport_set_measure_render_time` already does,
  no change to `configure()` for viewports whose dynamic flag is false. The FSR 2/TAA/auto-exposure preservation of
  §4 does change behavior on a *manual* scale change (it stops flashing); if "off must be bit-identical" includes
  manual changes, gate §4 on the dynamic flag (question Q1).
- **On**: the controller is a median of 30 floats per decision and a few comparisons per frame: microseconds. Two
  GPU timestamps per viewport per frame. A scale change: 0.4-2.3 ms CPU (§2.2), plus FSR 2 context creation no
  longer (§4.1). Memory with FSR 2: FSR 2's internal-size buffers at the max scale.
- **Perf guard**: unchanged when off by construction; the follow-up build runs it once with the flag off to show it.

## 10. What this round did not do

No engine code. No Safari, no Android, no throttled-GPU run (the CPU-bound case used a busy-wait, the GPU-bound case
a shader loop, which bracket the same behavior). The FSR 2 "no flash" check after §4.1 belongs to the build task.
