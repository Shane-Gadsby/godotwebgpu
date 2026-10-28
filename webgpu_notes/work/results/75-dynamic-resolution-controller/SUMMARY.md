# Task 75: dynamic 3D resolution controller, API and settings (SUMMARY)

Branch `work/75`, on `webgpu-4.8` at `8ee505d273`. Built to task 68's DESIGN.md and the user's answers Q2 and Q5-Q9.
Phases 1 (GPU time on WebGPU, task 73) and 2 (scale changes keep temporal state, task 74) were already merged.

## What changed

**The controller** lives in `RendererViewport` (`servers/rendering/renderer_viewport.{h,cpp}`, the `_dynamic_scale_*`
functions and the `Viewport::DynamicScale` state). It is off by default. While no viewport has it on, nothing of it
runs (`dynamic_viewport_count`). When on, it runs once per drawn frame, before the viewports draw:
- **GPU time** (DESIGN §3.2): each viewport's `vp_begin`/`vp_end` timestamps, which the controller turns on for its
  viewport. A median over 30 fresh samples drives the decisions:
  - over 95% of the budget: straight down to the scale the `a + b·s²` model puts at 85%;
  - up one step when the model says the next step stays under 80%;
  - misses while the GPU is under 75% of the budget: `CPU_LIMITED`, and the scale is never lowered for that.
  - `a` is refitted from two settled points at least 0.1 apart.
- **Frame time**, where there is no GPU time (§3.3):
  - A step down must deliver 25% of its predicted gain, or it is reverted as `CPU_LIMITED`. The retry back-off is
    5 s, doubling up to 60 s.
  - A step up needs the frame under 80% *and* the model's agreement. If it misses, it is reverted, and that scale is
    blocked for 10 s, doubling up to 120 s, unless the load has since dropped by a quarter.
  - **Q6:** on WebGPU, frames in flight (submit-to-done latency ÷ frame time) of 2.5 or fewer reports CPU-bound
    without a probe. 4 or more steps down without verifying.
  - **Q5:** frame-time viewports take turns, one per settle, largest first; reverts are exempt.
- **Shared rules:**
  - Settle after a change: max(0.25 s, 8 frames), plus twice the queue latency.
  - Startup: 3 s **and** 60 frames, and again after a scenario change or a first camera.
  - Hitch: over 4x both the budget and the running average.
  - Scales are quantized to a 0.05 step.
  - Verbose log lines on every change and state change.

**API** (`scene/main/viewport.{h,cpp}`, `servers/rendering/rendering_server*`):
- `Viewport` properties: `scaling_3d_dynamic`, `_target_fps` (60, Q7), `_min_scale` (0.5), `_max_scale` (1.0; Q8:
  above 1.0 allowed, Bilinear only, other modes clamp to 1.0), `_remember` (Q9).
- `Viewport` methods and signal: `get_scaling_3d_effective_scale()`, `get_scaling_3d_dynamic_state()` (OFF,
  STARTING, ADJUSTING, STABLE, CPU_LIMITED, AT_MIN_SCALE), `get_scaling_3d_dynamic_source()` (NONE, GPU_TIME,
  FRAME_TIME), and the signal `scaling_3d_effective_scale_changed(scale)`.
- `RenderingServer` has the same as `viewport_set_scaling_3d_dynamic*` and `viewport_get_scaling_3d_*`. The getters
  read atomics, with no render-thread sync, because `Viewport` polls them every frame while the controller is on.
- `scaling_3d_scale` stays the user's value and is the starting scale; setting it restarts the controller from it.

**Project settings:** `rendering/scaling_3d/dynamic`, `dynamic_target_fps`, `dynamic_min_scale`,
`dynamic_max_scale`, `dynamic_remember`. `SceneTree` applies them to the root viewport only, never in the editor.

**Remember (Q9)** saves to `user://scaling_3d_dynamic.cfg`:
- It saves on entering `STABLE`, in a section per rendering method + GPU name, keyed by the viewport's path.
- On load, the server's starting scale is set to the saved value. The property keeps the user's value, and turning
  dynamic off restores it.
- The class docs describe it, and the §8 do-it-yourself pattern.

**FSR 2:**
- The context is created at `target × max_scale`, through a new unbound
  `RenderSceneBuffersConfiguration::max_internal_size` → `RenderSceneBuffersRD::get_max_internal_size()` →
  `ensure_fsr2()`. Controller changes therefore never grow it (task 74's question).
- With the controller off the field is (0, 0), which keeps the old sizing.

**Q2:** FSR 1 with dynamic on WebGPU becomes Bilinear, with a one-time warning. **XR:** the controller stays off, with
a warning.

**WebGPU driver (Q6):** `RDD::get_submit_to_done_latency_usec()` is a new non-pure virtual; the base returns 0, and
only WebGPU implements it. It works like this:
- One `onSubmittedWorkDone` per frame, kept in its own in-order FIFO (`WGSubmitLatency`). It cannot share the frame
  fence's callback; see the trap in for-tasks.
- It is lazy: no callback is registered until something asks, so off means no extra calls.
- If the driver is freed with callbacks still pending, the last callback deletes the tracker (orphaned, as fences
  do).

**Docs:** `doc/classes/Viewport.xml`, `RenderingServer.xml` and `ProjectSettings.xml` are filled in, and `--doctool`
is idempotent.

**Volumetric fog** is left as today (Q4).

## Verification (actual numbers)

RTX 4080 SUPER on a 2560x1440 display at 165 Hz. Task 68's scene at 1920x1080, target 60 fps (16.7 ms), Bilinear
unless noted.
- Native runs: windowed on the real display for timing, under Xvfb with `--fixed-fps 60` for captures.
- Web runs: nothreads `template_release`, canvas asserted 1920x1080 (trap 98).
- Adapter, asserted at the start of every browser run: Chrome `nvidia/lovelace`, not fallback, 48 sampled textures;
  Firefox non-null, not fallback.
- "Settled" = from 15 s after the start or after the load change, to the next change or the end (`bench/rate.py`).

### Settles without oscillation (target: < 2 changes/min once settled)

GPU-bound load (`iters` 6000 native, 4500 web), dropping at 45 s (to `iters` 1500):

| run | source | changes/min, settled 15-45 s / 60-90 s | where it settled | after the load drop |
|---|---|---|---|---|
| native (final binary) | GPU | **0 / 0** | 0.75 | 1.0 within 1.6 s |
| native, FSR 2 (60 s) | GPU | **0** (15-60 s) | 0.90 | n/a |
| Chrome | GPU | **0 / 0** | 0.80 (one change at 4.5 s) | 1.0 within 1.5 s |
| Firefox | GPU | **0 / 0** | 0.80 | 1.0 within 1.6 s |
| Chrome, debug template (40 s) | GPU | **0** | 0.80 | n/a |

**One transient seen.** In 3 of the native/Firefox runs, a single window spiked within the first ~12 s (GPU 16.9-18 ms
for half a second). The controller stepped down and was back within about 1 s, 3 changes each. It is not in any
settled window.

### CPU-bound stays at full scale and reports CPU-limited

Busy-wait 24 ms per frame, light GPU:

| run | changes | state |
|---|---|---|
| native | 0 | `CPU_LIMITED` 93% of frames (rest STARTING/first window); frame 24.4 ms, GPU 2.4 ms |
| Chrome | 0 | `CPU_LIMITED` 94% |
| Firefox | 0 | `CPU_LIMITED` 94% |

**camera-sim**, from its existing `builds/` export copied to scratch:
- **Setup.** Engine files swapped for this branch's release template. An `override.cfg` plus `dyn_on.gd`, preloaded
  through `engine.preloadFile()`, turn the controller on for `/root/Main/SubViewport`: camera-sim draws its 3D
  there, so the root project setting would not reach it. Click Start; Chrome; canvas 1656x968 (the export keeps its
  window size, trap 98).
- **Target 60:** source GPU time, `STABLE` at **1.0**, 0 changes in 60 s; frame 6.1 ms, GPU 1.5 ms. camera-sim is
  no longer CPU-bound at 60 fps: it has been optimized since task 68's 12-30 ms.
- **Target 240** (to make it miss): `CPU_LIMITED` at **1.0**, 0 changes.
- 2 console errors, both camera-sim's own `Skin bind 'Root'` (trap 99).

### Web fallback with `timestamp-query` withheld (frame-time path)

| run | changes/min settled | result |
|---|---|---|
| Chrome, GPU-bound | **0 / 0** | Q6 saw a deep queue, so 1.0 → 0.85 with no probe; 1.0 within 1.0 s of the load drop |
| Chrome, CPU-bound | **0** | Q6: "2.0 frames in flight", CPU-bound reported with no probe; `CPU_LIMITED` |
| Firefox, GPU-bound | **0 / 0** | settled at 0.65 (see limits), 1.0 within 3.8 s of the load drop |
| Firefox, CPU-bound | **0** | Q6: 1.6-1.8 frames in flight, so no probe; `CPU_LIMITED` |

All with 0 console errors.

**Three bugs this verification found, now fixed:**
- **Absolute hitch filter.** A 56 ms scene at a 10 ms budget never filled a window, so the scale never moved.
- **Probing a scale it can't hold.** Before the model gate, the fallback probed and reverted 0.90 at **8/min**.
- **Failure memory outliving a load drop.** It held 0.85 for 34 s after the load dropped.

### Off = bit-identical, and perf_guard with it off

- **Bit-identical:** frames hashed (md5) against the pre-change editor (built first, copied aside), `--fixed-fps 60`,
  12 frames after 120. **9/9 configurations identical frame for frame** with the final binary:
  - Forward+: Bilinear 1.0 and 0.77; FSR 2 1.0 and 0.77; TAA 0.77; Bilinear + auto exposure 0.77; FSR 2 + auto
    exposure 0.77.
  - Mobile: Bilinear 0.77; Bilinear + auto exposure 0.77.
- **perf_guard** (final editor + release template, scenes exported under `xvfb-run`, real GPU): **PASS 4/4**.
  - postfx: 2 encoders, 2 submits, 59 writes, 347 KB
  - shadows: 11 writes, 141 KB
  - instances: 11 writes, 159 KB
  - pbr: 11 writes, 65 KB

  Every scene is within its ceiling.

### The FSR 2 capture around a controller-driven change (task 74's bench)

Native, Xvfb, `--fixed-fps 60`, HDR float frames. The change is driven by the controller: the target jumps from 30 to
1000 fps, or the run starts at 0.6 with room to grow. Task 74's light scene (`iters=64`). d = mean |frame −
previous|:

| change | steady d before | largest d in the 6 frames after | steady d after | luminance |
|---|---|---|---|---|
| 1.0 → 0.5 (down) | 0.00117 | 0.00069 | 0.00051 | −0.3% |
| 0.6 → 0.65 (up; the context is already at max, so no growth) | 0.00060 | 0.00084 | 0.00060 | −0.05% |
| 1.0 → 0.5, auto exposure on | 0.00119 | 0.00072 | 0.00053 | −0.3% (no flash) |

There is no FSR 2 pulse. Task 74's pre-fix pulse was 3-6x steady for 4 frames, and its growth pulse was 0.0029
against 0.0005.

### Remember round-trips across a restart

- **Native:** run 1 saved `/root=0.75` under `[forward_plus NVIDIA GeForce RTX 4080 SUPER]`. Run 2 logged
  "starts from its remembered scale 0.75", with `get_scaling_3d_effective_scale()` at 0.75 on the first frame.
- **Web (Chrome, page reloaded in the same profile, IndexedDB):** run 1 saved 0.80, and run 2 started at 0.80 with 0
  changes.

### Builds

All with 0 compiler warnings:
- Editor with `dev_mode=yes` (`-Wall -Wextra -Werror`), the full tree.
- Web `template_debug` and `template_release`: nothreads, no dlink, raycast.

Also clean: `pre-commit run --files <changed>`, and `--doctool .` (only the three class XMLs change).

### Not run (per the test scope)

- `smoke.sh`: no shader changed.
- The Forward+ matrix, the scene smoketest, threaded/dlink templates, Safari, Android, Compatibility, Mobile on the
  web, and a native frame-time run.

## Limits and risks

- **The frame-time fallback is coarse under vsync** (the design said so). At 165 Hz the frame is 12.1 or 18.2 ms, so
  Firefox's first verifying step (1.0 → 0.8) read as "no gain", was reverted as CPU-bound, and the next probe settled
  at 0.65 where the GPU path picks 0.80. It is stable (0 changes/min), just conservative. Chrome's deeper queue let Q6
  skip that verification.
- **Remember keys by renderer + GPU + viewport path**, not window size (question 1 in for-tasks).
- **A scale change still reallocates internal-size buffers.** On the web that can mean one 25-70 ms frame (DESIGN
  §2.2). Changes are rare once settled.
- **`lod_distance_multiplier` still reads the global `rendering/scaling_3d/scale`**, as before, not the effective
  scale.
- **The getters read atomics without a sync.** A `Viewport` freed while its RID is read is impossible from script; a
  stale read is at most one frame old.

## What the lead must still run

Merge, then the dev_mode editor and web `template_release` builds. To rerun anything, the bench is in `bench/`. Edit
`S=` (the scratch path) in its scripts and the templates in `export_presets.cfg`. Then:
- `native_batch.sh`, `web_batch.sh`, `off_batch.sh` (needs a pre-change editor in `$S/pre/`) and `perf_guard.sh`;
- `bench/camsim/` holds camera-sim's probe.
