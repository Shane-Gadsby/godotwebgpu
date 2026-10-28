# Task 68: for the lead (questions for the user, the build task, camera-sim)

## Questions for the user (defaults marked; the design in DESIGN.md assumes the defaults)

- **Q1. Should a manual `scaling_3d_scale` change also stop flashing?** DESIGN.md §4 keeps the FSR 2 context, TAA
  history and auto-exposure luminance across a scale change. Default: **yes, for every change** (today's flash is a
  bug for any script that changes the scale), which means "off" is bit-identical per frame but a manual change looks
  better than today. Alternative: only when `scaling_3d_dynamic` is on, so off is bit-identical in every case.
- **Q2. Dynamic + FSR 1 on WebGPU** (FSR 1 renders black there): default **refuse with a warning and run Bilinear**;
  alternative: refuse and leave it off.
- **Q3. TAA across a change**: default **resample the history** into the new size (one blit); alternative: keep
  today's reseed (one or two mild frames, measured).
- **Q4. Volumetric fog**: default **leave as today** (its temporal reprojection restarts on a change); alternative:
  pin the froxel grid to the max scale.
- **Q5. Several dynamic viewports without GPU time** (frame time is global): default **one viewport changes per
  settle period, largest first**; alternative: only the root viewport may use the fallback.
- **Q6. Submit-to-done queue depth as a second fallback signal** (GPU-bound 3-8 frames in flight, CPU-bound ~2):
  default **no, not in the first build** (Firefox's margin is thin, browser queueing is unspecified); alternative:
  use it to skip the CPU-bound probe when it is clear.
- **Q7. Target as fps or ms**: default **fps** (`scaling_3d_dynamic_target_fps = 60`); alternative: a frame time.
- **Q8. Max scale above 1.0** (supersample with Bilinear when there is room): default **allowed, default 1.0**;
  alternative: clamp to 1.0.
- **Q9. Does the engine persist the settled scale?** Default **no**, the project's job (DESIGN.md §8); alternative:
  an opt-in `scaling_3d_dynamic_remember` that saves it per machine in `user://`.

## Proposed follow-up build task (outline)

1. **WebGPU GPU time** (prerequisite): timestamps through pass `timestampWrites` (not `writeTimestamp()`, absent in
   Firefox), a small ring of readback buffers polled with `RDD::buffer_get_readback_status()` (task 66), re-enable
   `viewport_get_measured_render_time_gpu()` on the web. Check: the bench's sweep reports engine GPU time in Chrome
   and Firefox within 10 % of the page hook.
2. **Keep temporal state across a scale change** (§4): one FSR 2 context per viewport with `maxRenderSize` at the max
   scale, TAA history resample, auto-exposure luminance carried over. Check: the native capture of DESIGN.md §2.3
   (`proto/main.gd mode=capture`) shows no luminance jump and no FSR 2 pulse; then the same on the web.
3. **Controller** in `RendererViewport` (§3), API on `Viewport`/`RenderingServer` + project settings (§5), docs
   (class reference, the persisting pattern of §8).
4. **Verification** (the spec's): settle without oscillation (< 2 changes/min settled) on a GPU-bound scene natively,
   Chrome, Firefox; CPU-bound scene and camera-sim stay at full scale and report `CPU_LIMITED`; web fallback with
   `timestamp-query` withheld; off = bit-identical (perf guard once with it off); FSR 2 capture around a change; dev_mode
   editor and web `template_release` builds; smoke.sh only if a shader changes.
Estimated size: 1 (WebGPU timestamps) and 2 are a day each; 3 two days; 4 a day.

## Note for the camera-sim session

What exists after this round: a design, not a feature. Nothing changes for camera-sim yet.

What was measured on camera-sim's current web export (RTX 4080 SUPER, Chrome): frame time 12-30 ms with the GPU
busy 2.4-5.8 ms per frame. The frame is **CPU-bound**, so lowering the resolution cannot help, and its "Auto" had
stepped all the way down to **700p** (the bottom of its list) on a fast GPU: every size failed for a reason
resolution does not control. Until the engine feature lands, a cheap fix in camera-sim's Auto is to keep the
largest size when a step down does not shorten the median frame by a meaningful amount (DESIGN.md §3.3: at least a
quarter of the expected gain), instead of continuing down the list.

When the build task lands, Auto becomes: the SubViewport sized to the panel's real screen pixels (Match screen),
`scaling_3d_dynamic = true`, `scaling_3d_dynamic_target_fps = 60`, `scaling_3d_dynamic_min_scale` = 720 / panel
height (so it never goes under 720p), and the label from `get_scaling_3d_effective_scale()` x panel height ("Auto
(1080p)") plus "limited by CPU" from `get_scaling_3d_dynamic_state()`. Its timing code (`start_auto_resolution`,
`update_auto_resolution`, the candidate list) goes; `display.cfg` keeps only the settled scale per machine, browser
and window mode, written back to `scaling_3d_scale` at start (DESIGN.md §8). camera-sim uses TAA, which the design
keeps across changes.
