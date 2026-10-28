# For TASKS.md (work task 57, local exposure)

## §4 entry

### 4.xx Local exposure (October 2026) *(work task 57; survey §2.48, `pp-local-exposure`)*

A **Local Exposure** group on `CameraAttributes` (both kinds; `local_exposure_*`, off by default): highlight contrast
(0.6), shadow contrast (0.8), detail strength (1.0), middle gray bias (EV, 0), blurred luminance blend (0.6) and
kernel size (% of frame width, 50). RS `camera_attributes_set_local_exposure()` and
`_set_local_exposure_blurred_luminance()`. Unreal's Bilateral method: the exposed log2 luminance (Rec. 709 Y,
relative to 0.18) is split into a base, a bilateral grid (Chen, Paris and Durand 2007: 16 square cells down the
frame, 32 range bins of 1 EV, binomial [1 4 6 4 1] blur per axis, trilinear slicing) mixed with a plain Gaussian
blur, and the detail; new = M + (B - M) * contrast + (L - B) * detail (model block "Local exposure" in
`camera_attributes_storage.cpp`). Sizes relative to the frame. `CameraLocalExposure` (`camera_local_exposure.glsl`,
build + blur compute, after DOF/motion blur/glare, with the tonemapper's exposure incl. auto exposure; sums by
workgroup atomics in fixed point since WGSL has no float atomics) writes an RGBA16F 3D grid of 33 slices (the last
is the blurred luminance), sampled by the Forward+ tonemapper at **set 1 binding 3** right after exposure and FXAA,
before glow/glare/vignetting/white balance/grain. **CameraEffectsUBO: `header.w` bit 0 and `local_exposure[2]`
(the former `reserved[0..1]`; `reserved` is now `[13]`)**: [0] = highlight, shadow, detail, blend; [1] = middle gray
log2, grid UV scale x, y, unused. Mobile, Compatibility and multiview: not applied, documented (a one-time editor
warning in Mobile/Compatibility). Matrix arm `local_exposure`.
Verified (worker): defaults pixel-identical to the pre-change editor in 12/12 renders (3 renderers x filmic+glow,
filmic+AE, linear, physical+AE), and Mobile/Compatibility with it on = off; against an independent numpy model, a
window-and-room scene and a 13-patch chart at 720p/1080p/1440p within p99 0.0015 / max 0.0027 EV, 11 control
settings plus AE and physical units within p99 0.0032 / max 0.0064 EV; 720p vs 1440p factor maps agree to p99
0.004 EV; edge halos equal the model's (0.27 EV under the window at the default blend, 0.17 at blend 0); WebGPU
vs native Forward+ in 8 cases at 720p, away from edge pixels where the two already differ with it off: mean
0.03-0.06/255, max 3-5 (18 at isolated pixels); WebGPU 1080p GPU cost ~0.17 ms (build 0.04, blur 0.12, tonemap +0.013, RTX
4080 SUPER idle clocks); matrix arm passes `--parity` (delta 11.39, native diff 0.002/0.004).

## §3 trap candidates

- **A scratch test scene can render the outside of its own room** (2026-10-28, work task 57): a camera 0.4 m behind the
  back wall of a box room saw only the wall's outer face, and the first "model matches to 0.002 EV" numbers were for
  that flat wall (the factor varied by 0.07 EV across the frame). Look at a frame of a new test scene before
  trusting numbers from it, and check the factor's range is what the scene should produce.
- **An HDR 2D viewport's `get_image()` is RGBH (3 channels, format 14), not RGBAH**: size raw readbacks from the data
  length, not an assumed 4 channels.
- **An HDR 2D viewport's `get_image()` returns null on the web** (WebGPU, template_debug, no error printed), while it
  works natively: a web test cannot read HDR values back that way, so task 57 compared WebGPU with native in 8 bits
  (native carries the HDR comparison against the model). Not investigated; a §2 candidate if HDR readback on the web
  matters to anyone.
- **`pgrep -f` / `pkill -P $(pgrep -f name)` in a tool shell matches the shell itself** (memory "pkill self-match");
  it killed a whole GPU job here. Kill by the PIDs `ps aux | grep [n]ame` prints.

## §2 follow-ups (optional)

- The blur pass is a single 5x5x5 pass (125 loads per cell), 0.12 ms of the 0.17 ms; separable would cut it to ~15
  loads (3 dispatches). The build pass serializes on workgroup atomics when a cell is flat (one bin); per-thread
  pre-binning would help. Worth it only if a phone profile shows the cost.
- **Exposure Fusion** (Mertens et al. 2007, Unreal's second method) fits later without a new binding: its result is a
  low-resolution log-exposure map, which can go in the grid texture's last slice with a `header.w` mode bit; cost
  roughly 2-3x the bilateral method (a Laplacian pyramid of ~3 synthetic exposures).

## Questions for the user

- `local-exposure-defaults` (choice): default contrasts when the group is turned on.
  - **Highlight 0.6, shadow 0.8, blend 0.6 (current, default)**: a gentle lift; a window 8.6 EV over middle gray
    comes down 1.4 EV, the darkest tenth of the room comes up 0.86 EV.
  - Unreal's own (1.0 / 1.0 / 0.6): turning it on does nothing until a contrast is lowered.
  - Stronger (0.4 / 0.7, blend 0.3): windows come down ~3 EV out of the box; more visible halo at strong edges.
- `local-exposure-blend` (choice): the blurred luminance blend default.
  - **0.6, Unreal's default (default)**: smooth, but it limits how far a bright window comes down (−1.4 EV vs −3.3 at
    0) and leaves a 0.27 EV darker band on the wall under it.
  - 0.3: halfway.
- `look-local-exposure` (look): `look_montage.png` (off / defaults / strong), Forward+ native; WebGPU repeats it.
