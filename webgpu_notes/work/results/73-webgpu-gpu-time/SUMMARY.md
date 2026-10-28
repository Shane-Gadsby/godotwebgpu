# 73: GPU time on WebGPU (dynamic resolution phase 1)

## What changed

`drivers/webgpu/` only (plus one doc note), so native builds are unchanged by construction: `drivers/webgpu/` is
compiled only into `platform=web webgpu=yes` builds.

- `timestamp_supported` now follows the device's `timestamp-query` feature, which `engine.js` already requests
  whenever the adapter offers it. The render-thread device copies the main device's features, so it gets it too.
- `command_timestamp_write()` writes the timestamp as the **end-of-pass** write of an empty compute pass
  (`timestampWrites`). It no longer uses `wgpuCommandEncoderWriteTimestamp`, which is Chrome-only and missing in
  Firefox. End of pass, so the timestamp lands after the work before it, like Vulkan's bottom-of-pipe write.
  The beginning-of-pass write goes to one spare query at the end of the set: emdawnwebgpu passes
  `WGPU_QUERY_SET_INDEX_UNDEFINED` through to JS as 4294967295 instead of leaving the member out, which is a
  validation error.
- `WGQueryPool` (one per RenderingDevice frame slot) has a ring of 4 `MAP_READ` buffers:
  - `command_buffer_end()` resolves only the written queries and copies them into a FREE slot.
  - After the submit, `command_queue_execute_and_present()` calls `mapAsync` on that slot.
  - The callback copies the results to the CPU only if they are newer than what is there, then unmaps and frees
    the slot.
  - A pending map is never unmapped or recorded into. The old code did both, which is why the readback was switched
    off.
  - If every slot is still in flight, that frame's timestamps are dropped. Nothing ever waits.
  - A command buffer that was begun again or never submitted gives its slot back.
  - A pool freed while maps are pending is deleted by the last callback.
- `timestamp_query_pool_get_results()` calls `wgpuInstanceProcessEvents` (non-blocking) and returns the newest
  results that have landed. On the web these come from an earlier frame of the same slot. It returns them only when
  their timestamp count equals RenderingDevice's count for this frame; otherwise it returns zeros, because a
  different count means the capture layout changed and the names would not line up.
- `doc/classes/RenderingServer.xml`, `viewport_get_measured_render_time_gpu`: a note for the web (needs
  `timestamp-query`, 0 without it, a few frames late).

## Verification (RTX 4080 SUPER, 1920x1080 page, task 68's bench `iters=3000` bilinear sweep)

Release template unless noted; numbers are ms. Every run asserted its adapter: Chrome `nvidia/lovelace`, not
fallback, 48 sampled textures; Firefox non-null, not fallback. Every run had **0 console errors**. Raw data is in
`data/`; the bench copy (with `measure=`, `--nohook`, `--withhold`) is in `bench/`.

**Engine GPU time against the page hook** (`gpu_mean` = hook, `engine` = `viewport_get_measured_render_time_gpu`):

| scale | Chrome hook / engine | Firefox hook / engine | Chrome debug template hook / engine |
|---|---|---|---|
| 1.00 | 12.85 / 12.69 (-1.2%) | 12.75 / 12.65 (-0.8%) | 12.89 / 12.70 (-1.4%) |
| 0.85 | 10.01 / 9.81 (-2.0%) | 9.92 / 9.68 (-2.4%) | 10.07 / 9.96 |
| 0.70 | 7.00 / 6.89 | 6.86 / 6.80 | 6.98 / 6.92 |
| 0.60 | 5.48 / 5.38 | 5.49 / 5.44 | 5.51 / 5.43 |
| 0.50 | 3.77 / 3.78 | 3.97 / 3.93 | 3.88 / 3.83 |

All within 2.5%, against the 10% bar. The engine number is slightly lower because the hook also times the 2D/blit
work outside the viewport.

**No stall: median frame time** with measurement on (two timestamps per frame) against off, no page hook:

| scale | Chrome on / off | Firefox on / off |
|---|---|---|
| 1.00 | 13.0 / 13.0 | 14.5 / 14.6 |
| 0.85 | 9.7 / 9.7 | 11.1 / 11.7 |
| 0.70 | 6.9 / 6.9 | 8.3 / 8.3 |
| 0.60 | 6.1 / 6.1 | 6.7 / 7.1 |
| 0.50 | 6.1 / 6.1 | 6.1 / 8.3 |

The 6.1 ms floor is the 165 Hz vsync. Firefox's "off" run was the noisier of the two.

**`timestamp-query` withheld** (the page strips it from `requestDevice`; logged `device timestamp-query=false`): in
Chrome and Firefox the engine GPU time is 0.00 at every scale, 0 console errors, and frame times are the same as
above.

**Visual Profiler data**: the release export run with `--gpu-profile` (it prints the same `frame_profile` the Visual
Profiler shows) reports per-pass GPU times every second in Chrome, for example `GPU PROFILE (total 12.9ms)`: Depth
Pre-Pass 5.85, Opaque Pass 5.83, Sky 0.22, Setup Shadows 0.36, SSR 0.14, Glow 0.08... The Visual Profiler adds about
40 timestamps a frame, and the frame time was still 13.0 / 9.7 / 7.1 / 6.1 ms.

**Builds**: `wgsl_convert_cli`; editor with `dev_mode=yes` (0 warnings); web `template_debug` and
`template_release` (nothreads, no dlink, raycast; 0 warnings each).

## Skipped (per the user's test scope)

- `smoke.sh`: no shader interface changed.
- Forward+ matrix, scene smoketest, perf_guard.
- The editor's Visual Profiler by hand over remote debugging. I checked its data source, `frame_profile`, through
  `--gpu-profile` instead.
- A native run: no file built natively changed.
- Safari, Android and threaded templates: not built or run. The render-thread device inherits the feature, but that
  is untested.

## For the lead to run

- Merge, then build the web `template_release`.
- Optionally, rerun one Chrome sweep: `bench/runs.sh` needs its scratch paths (`S=`) and the export preset's
  template paths pointed at the merged tree.

## Risks

- **Results lag a few frames on the web.** Within one frame slot, the indices are trusted when the count matches.
  If two measured viewports swapped draw order but kept the same count, a few frames could pair the names with the
  previous layout's values. That is rare, and it self-corrects once newer data lands.
- **A deeper queue than the ring.** With more than 4 readbacks in flight per slot, some frames' timestamps are
  dropped (newer data just arrives less often). How often this happened was not instrumented. The profiler printed
  a profile every second in every run, so the data kept arriving.
