# Task 74: a scaling_3d_scale change keeps auto exposure, FSR 2 and TAA (SUMMARY)

Branch `work/74`, one code commit on `webgpu-4.8` at `1ff3d995b5`. Renderer only (`servers/rendering/renderer_rd/`),
no driver, shader or interface change.

## What changed

`RenderSceneBuffersRD::configure()` (a 3D scale change, a resize, an MSAA toggle...) used to `free_data()` every
`RenderBufferCustomDataRD` and free every named texture, so all temporal state restarted. Now:

- **`RenderBufferCustomDataRD::free_data_for_reconfigure()`** (new virtual, default = `free_data()`): configure()
  calls it instead of `free_data()`. `free_data()` still frees everything; it runs on destruction.
- **Auto exposure**: `Luminance::LuminanceBuffers` keeps `current` (the adapted 1x1 luminance) and rebuilds only the
  size-dependent reduce chain. `CameraMetering::MeteringBuffers` keeps the auto white balance state the same way.
- **FSR 2**: `RenderBufferDataForwardClustered` keeps `fsr2_context` while the mode is still FSR 2 and the target size
  is the same. The context accepts any render size up to the one it was made with (`maxRenderSize`), so a smaller
  scale just dispatches with a smaller `renderSize`. A scale **above** the context's maximum grows it in
  `ensure_fsr2()`: a new context at the new size, and `FSR2Effect::transfer_history()` copies every resource FSR 2 keeps
  between frames (upscaled history, locks, luma history, auto exposure; the render-size motion vectors and masks as
  their top-left region) plus the frame state (ping-pong index, frame index, jitter, pre-exposure). The context keeps
  the largest render size the viewport has used, so growing happens once per new maximum. A first context is still
  created at the current internal size: that is what keeps unchanged viewports bit-identical (FSR 2's `ClampUv()`
  rounding and luma-mip sizes depend on `maxRenderSize`).
- **TAA**: configure() carries `taa/history` and `taa/prev_velocity` over (new `carried_textures` in
  `RenderSceneBuffersRD`), and `TAA::process()` resamples them bilinearly into the new size with `copy_to_fb_rect()`
  (one draw each per view), then resolves as usual. Only resampled textures get `COLOR_ATTACHMENT`; a viewport that
  never reconfigures creates exactly the textures it always did.

**Still resets on a scale change** (listed, not changed): volumetric fog's froxels and temporal reprojection (Q4, as
decided); SSIL's last-frame buffer (one frame of reduced indirect light, not visible in the bench); FSR 2 on a target
size change or when leaving FSR 2; MetalFX temporal's context (Metal only, untouched); TAA when the view count
changes. Compatibility is unaffected (GLES3 has its own buffers).

## Verification (actual numbers)

Bench: task 68's `proto/main.gd` capture, reworked (`bench/` here): root viewport **HDR 2D, Linear tonemapper, float
readback** (`get_image_async()`, RGBH), mean luminance L and mean |frame - previous| d, scale changed at frame 0, static
camera. Native: Vulkan under Xvfb, `--fixed-fps 60` (deterministic: the new build twice gave identical hashes), 4080
SUPER. "before" = editor/template built from `1ff3d995b5` in the same worktree; "after" = this branch.

**Native, Forward+** (max over the frames after the change):

| run | before: max L change / max d | after: max L change / max d | steady d |
|---|---|---|---|
| bilinear + auto exposure, 1.0 -> 0.6 | **+285 %** / 0.660 (still re-adapting at frame 20) | 0.2 % / 0.0037 | 0 |
| bilinear + auto exposure, 0.6 -> 1.0 | **+285 %** / 0.660 | 0.1 % / 0.0037 | 0 |
| FSR 2 + auto exposure, 1.0 -> 0.6 | **+285 %** / 0.662 | 0.2 % / 0.0010 | 0.0013 |
| FSR 2, 1.0 -> 0.6 (kept context) | 0.3 % / **0.0080** (frames 0-3 at 3-6x steady) | 0.2 % / 0.0010 | 0.0013 |
| FSR 2, 0.6 -> 1.0 (grown context, history transferred) | 0.6 % / **0.0079** | 0.3 % / 0.0029 (one frame) | 0.0005 |
| TAA, 1.0 -> 0.6 | 0.1 % / 0.0039 | 0.1 % / 0.0021 | 0.0002 |
| TAA, 0.6 -> 1.0 | 0.5 % / 0.0044 | 0.1 % / 0.0005 | 0.0002 |
| bilinear, 1.0 -> 0.6 (reference: the resolution change itself) | 0.2 % / 0.0038 | 0.2 % / 0.0038 (identical) | 0 |
| Mobile, bilinear + auto exposure, 1.0 -> 0.6 | 0.1 % / 0.0042 | identical | 0 |

The native L clips at 1.0 in the "before" flash frames (the viewport's linear output saturates), so +285 % understates
it. After the change every residual d is at or under the bare resolution change's own 0.0038. Mobile showed no flash
in either build (its auto exposure did not visibly adapt in this bench). Per-frame tables:
`bench/table.py data/native/chg_<run>_base.txt data/native/chg_<run>_new.txt`.

**Web, Chrome 156 (Vulkan; adapter asserted nvidia/lovelace, not fallback, 48 sampled textures: 12/12 runs)**,
nothreads `template_release`. One readback in flight at a time (see findings), so samples are ~3 frames apart and
compared with the last, settled sample (d_final):

| run | before: max L change / max d_final (12 frames) | after |
|---|---|---|
| bilinear + auto exposure, 1.0 -> 0.6 | **+396 %** / 0.0295 | 0.0 % / 0.0000 |
| FSR 2 + auto exposure, 1.0 -> 0.6 | **+418 %** / 0.0396 | 0.1 % / 0.0006 |
| FSR 2, 1.0 -> 0.6 and 0.6 -> 1.0; TAA both ways | 0.1-0.2 % / 0.0001-0.0004 | 0.1 % / 0.0001-0.0006 |

The auto exposure flash is gone on the web too. The **FSR 2/TAA pulse cannot be resolved on the web** with today's
readback: the web capture reads ~11x darker than native in both builds, so the pulse (~0.008 natively) sinks into
the noise of samples 3 frames apart, and a frame-to-frame pair needs two readbacks in flight, which fails (findings).
The renderer code is the same as native, where the pulse is measured gone; the web shows no regression.

**No scale change, bit-identical** (md5 of each float frame, 12 frames after 120, `--fixed-fps 60`): Forward+ bilinear
1.0, bilinear 0.77, FSR 2 0.77, FSR 2 1.0, TAA 0.77, bilinear + auto exposure 0.77, FSR 2 + auto exposure 0.77; Mobile
bilinear 1.0, bilinear 0.77, bilinear + auto exposure 0.77: **12/12 frames identical in all 10**. Compatibility not
run: unaffected (no code in `drivers/gles3`).

**Forward+ matrix `--parity`** (new editor + nothreads release template, unbaked, `WEBGPU_REAL_GPU=1`): `taa` PASS
(delta 0.542, native diff 0.006), `scaling_3d` PASS (1.388, 0.002), `fsr2` PASS (1.580, 0.142 within its documented
tolerance), `auto_exposure` PASS (29.308, 0.003). 4/4, `data/matrix_parity.log`.

**Builds**: editor (`accesskit=yes webgpu=yes gdscript_braces=yes linker=mold`), the same with `dev_mode=yes`
(werror): clean, no warnings. Web `template_debug` and `template_release` (nothreads, no dlink, raycast): clean.
`pre-commit run --files <changed>`: clean.

**Skipped** (test scope): `smoke.sh` (no shader or interface change; TAA's resample reuses the existing `copy_to_fb`
pipeline, on an RG16F target for the first time), the full matrix, scene smoketest, perf_guard, Firefox, Mobile on the
web (software adapter only).

## Findings outside this task (for task 71 / the lead)

1. On WebGPU, `get_image_async()` on the root viewport logs one `GPUValidationError: [Buffer] used in submit while
   pending map` per call, in the base build too, and with one request per frame the callbacks stop arriving (a
   40-frame capture timed out at 240 s). Two requests in flight: `ERROR: An asynchronous GPU readback failed; 1
   callback(s) receive an empty array.` and a stall. One at a time works.
2. The web's float readback of the HDR 2D root viewport reads ~11x darker than native (L 0.031 vs 0.354, Linear
   tonemapper, same scene), in both builds; the matrix's auto_exposure parity (screenshots) is fine, so it looks like
   the readback path, not rendering.

## What the lead must still run

Nothing beyond the usual merge checks. The bench scripts in `bench/` point at this worker's scratch path (`S=` in
`run_native.sh`, `native_batch.sh`, `web_batch.sh`); edit it to rerun.

## Risks

- **FSR 2 growth relies on FSR 2's private context layout** (`ffx_fsr2_private.h`). A vendored FSR 2 update that changes
  it or adds a persistent resource must update `transfer_history()`'s list; a missed resource costs a one-time reset
  on growth, nothing worse.
- Memory: an FSR 2 viewport whose scale went up and back down keeps FSR 2's render-size buffers at the largest size it
  used (the design's intent for dynamic resolution, DESIGN §4.1).
- TAA's resampled previous velocity is in UV units (taa_resolve.glsl scales by the resolution), so resampling it is
  exact for slow motion; a fast camera on the change frame gets the usual disocclusion response.
- The first reconfigure of a TAA viewport compiles one new `copy_to_fb` pipeline variant (RG16F) synchronously; on the
  web that is one createRenderPipeline on that frame.
