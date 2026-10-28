# Task 74: for TASKS.md (lead copies in)

## §4 entry

**4.x Scale changes keep temporal state** (2026-10-28, work task 74, phase 2 of dynamic resolution, §2.x / task 68
DESIGN §4). A `scaling_3d_scale` change (any render buffers reconfigure) no longer restarts auto exposure, FSR 2 or
TAA. `RenderBufferCustomDataRD::free_data_for_reconfigure()` lets custom data keep size-independent state over
`RenderSceneBuffersRD::configure()`: Luminance's adapted 1x1, CameraMetering's AWB state, Forward+'s FSR 2 context
(kept while the target size and mode hold; a larger render size grows it once and `FSR2Effect::transfer_history()`
copies its history across). TAA's history and previous velocity are carried as named textures and resampled
bilinearly by `TAA::process()`. Native capture (HDR float, 1.0 <-> 0.6): auto exposure flash +285 % -> 0.2 %, FSR 2
frame-to-frame pulse 0.0080 -> 0.0010 (down) and 0.0079 -> 0.0029 (up, grown context), TAA 0.0044 -> 0.0005; web
(Chrome) auto exposure +396-418 % -> <=0.1 %. No-change renders bit-identical in Forward+ and Mobile (10 configs).
Matrix `taa`, `scaling_3d`, `fsr2`, `auto_exposure` pass with `--parity`. Still resetting: volumetric fog (Q4), SSIL's
last frame, FSR 2 on a target size change, MetalFX temporal. Results: `results/74-scale-change-temporal-state/`.

## Trap candidates (§3)

- **Per-frame `get_image_async()` stalls on WebGPU** (cost ~30 min): one request per frame logs `used in submit while
  pending map` per request and the callbacks stop arriving; two in flight fail with "asynchronous GPU readback failed"
  and stall. A web capture bench must keep one readback in flight at a time (then samples are ~3 frames apart: no
  frame-to-frame difference on the web). For task 71.
- **A bit-identical check needs `--fixed-fps 60`**: auto exposure's adaptation step is speed x frame time, so without
  a fixed step two runs of the same build differ. With it, they hash identical frame for frame.
- **A temporal effect's state must survive reconfigure on purpose**: anything per-viewport and size-independent that
  should not restart on a 3D scale change overrides `free_data_for_reconfigure()`; the default still frees it.

## Questions for the user (with defaults)

1. **FSR 2 context size for manual changes** - today it starts at the current render size and grows (once, history
   carried) when the scale goes above it, keeping the largest size used. Alternative: always allocate at the target
   size (more VRAM from the start for every FSR 2 viewport below 1.0, and no longer bit-identical to before for
   unchanged viewports). **Default: grow on demand (as built).** Task 75 should create it at its dynamic max_scale.
2. **SSIL's last-frame buffer** resets on a scale change (one frame of reduced indirect light; not measurable in the
   bench). Carry it like TAA's history? **Default: leave it.**

## For task 75 (the controller)

- `RenderBufferDataForwardClustered::ensure_fsr2()` creates the first context at the current internal size; give it
  the dynamic max (target size x `scaling_3d_dynamic_max_scale`, clamped to the target for FSR 2) so the controller
  never triggers a growth.
- A scale change still reallocates every internal-size texture (DESIGN §2.2's hitch); only the temporal state is kept.
