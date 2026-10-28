# Task 63: physical DOF blur sized by the lens: summary

## What changed

- **`CameraAttributesPhysical.dof_physical_blur_size`** (bool, default `false`, group "Depth of Field"). On: the blur is
  the thin-lens circle `f² / (N (u − f)) · |d − u| / d`, a share of the frame. `_update_frustum()` sends `c∞ / frame_mm`
  (frame = sensor height, or desqueezed width when `is_fov_horizontal()`) through the new
  `RenderingServer.camera_attributes_set_dof_physical_blur_size(rid, enable, frame_fraction, frame_horizontal)`
  (storage block `/* Physical DOF blur size */`, `camera_attributes_get_dof_physical_blur_radius(rid, buffer_size)`).
  Off: the `/ 5` scale stays, pixel-identical to before.
- **`BokehDOF::_physical_blur_size()`** turns it into pixels of `base_texture_size` (the buffer being blurred: internal
  size, or the upscaled one with FSR 2 / MetalFX temporal) in `bokeh_dof_compute()` (Forward+) and `bokeh_dof_raster()`
  (Mobile); tilt-shift and longitudinal CA use the same size.
- **The 32 px cap:** for opt-in cameras it becomes
  `rendering/camera/depth_of_field/depth_of_field_physical_max_blur_size` (percent of the buffer height, default 3% =
  upstream's 32 px at ~1080p; `GLOBAL_GET_CACHED`). **A visible clamp ships**, not a hidden cost; costs below.
  Non-opt-in cameras keep exactly 32 px.
- **Box/Hexagon gather fix, opt-in only** (`BOKEH_PHYSICAL_SIZE` = `shape_flags` bit 12; push constant unchanged at 128
  bytes, no binding change). Measuring the opt-in showed upstream's Box/Hexagon physical-mode DOF barely blurs:
  `weighted_filter_dir()` adds the center color for every sample outside the blur and divides by all 2·steps+1, which is
  right only when the blur fills the reach, and the physical reach is a fixed 32 px. A 5.5 px blur came out as a
  1.5 px 10–90% edge (4 of 25 samples). Under the flag, `bokeh_physical_size_resolve()` divides by the sample positions
  inside the largest blur reaching the pixel. That is the same estimator with a per-pixel reach, identical where the
  blur fills the reach. Half-size passes (Very Low/Low) also convert the full-res sizes in alpha to half-res pixels;
  upstream compares them directly and blurs twice as wide. Circle was already exact. Both shader copies
  (`bokeh_dof.glsl`, `bokeh_dof_raster.glsl`), helpers in `bokeh_dof_inc.glsl`.
- Docs: `CameraAttributesPhysical.xml`, `ProjectSettings.xml`, `RenderingServer.xml`. Compatibility has no DOF
  (documented).
- Not done: optional `sensor_fit`. It is not small: the storage's frame geometry (`_camera_frame_mm()`,
  `_fill_effects_lens()`, glare) and Camera3D's keep_aspect derive the fit from the squeeze; it wants its own task.

## Verification (actual numbers)

Native Vulkan, RTX 4080 SUPER, edge plane (white 0.5 | black 0.02, unshaded, Linear tonemapper, no clipping),
`tools/dof_test.gd` + `tools/edge.py` (2σ radius of the line spread; 10–90% width w), `tools/model.py` for the
predictions (disc / box ±L / hexagon ±0.866 L).

| Check | Model | Measured |
|---|---|---|
| Formula: 50 mm f/1.4, u 2.5, d 5, FF, 1280×720, circle | r 5.47, w 7.51 | r 5.69, w 7.62 (F+); 5.64 / 7.77 (Mobile) |
| same, hexagon (all 4 qualities, F+ / Mobile) | w 7.57 | 7.06–7.75 / 6.95–7.77 |
| same, box (all 4 qualities) | w 8.75 | 8.33–8.62 / 8.60–9.06 |
| Resolution 2560×1440, circle / hexagon Low | r 10.93 / w 15.15 | 10.97 / 14.49 (F+), 10.93 / 15.04 (Mobile) |
| MFT 17.3×13 (24/13 = 1.85×), circle / hexagon | r 10.09 / w 13.98 | 10.12 / 13.16 (F+), 10.12 / 13.03 (Mobile) |
| Near field u 8, d 3, circle / hexagon / box | 5.62 / w 7.78 / w 8.99 | 5.74 / 8.04 / 9.04 |
| Bilinear 3D scale 0.5 (frame-relative) | ~5.5 output px | circle 4.88, hexagon 6.03 |
| Cap: 200 mm f/2.8 u 5 d 50, 1440p, circle | 80.4 (cap 3% = 43.2) | 42.76 at 3%, 79.53 at 8% |
| WebGPU (Chrome, 1280×720) circle / hexagon / box / MFT circle | 7.51 / 7.57 / 8.75 / r 10.09 | 7.65 / 7.93 / 8.54 / 10.13 |

Before the gather fix, the opt-in hexagon at Medium read w 1.15 and Low w 7.66 (a quarter of the samples, and half-size
twice as wide); that is how the dilution was found. Known exception: Circle **Very Low** cannot draw a blur under ~8 px
(its first sample ring is at 8 px; upstream; with and without the opt-in), documented. Circle Low reads ~15% wide.

**Opt-in off, bit-identical:** pre-change editor (built from the base commit before any edit) vs new, 10/10 renders
maxdiff 0: Forward+ and Mobile × {physical hexagon Low, physical circle Medium, physical box High near field, practical
hexagon (positive transitions), practical circle in physical mode}. **Web:** a baseline template_debug built from the
base commit (detached worktree `godotwebgpu-63-base`) + pre-change editor vs the new pair, Chrome (adapter
nvidia/lovelace, 48 textures, asserted), 3/3 maxdiff 0.

**A/B against camera-sim** (scratch copy; `tools/ab63.gd`; its camera_studio scene at 1244×700, manual focus,
jitter and grain off; Practical arm = its own `update_dof()`; Physical arm = the same attributes copied onto
`CameraAttributesPhysical`, sensor 36×20.25, opt-in on; FOV 39.6° horizontal = 22.9° vertical). Face edge energy
ratio Physical / Practical (`tools/edge_energy.py`; crops in `camsim_ab_faces.png`):

| | f/8 u 5 | f/1.4 u 5 | f/1.4 u 2.5 |
|---|---|---|---|
| Circle shape (same gather both arms) | 1.019 | 1.013 | 1.021 |
| camera-sim's High Hexagon | 1.021 | 1.003 | **0.156** |

The sizes match camera-sim's math to 2%. The hexagon difference at f/1.4 u 2.5 is the gather fix: camera-sim's
current look goes through the diluted upstream hexagon (its defocused face keeps 6.4× the thin-lens edge energy); the
opt-in hexagon (9.5e-5) matches the circle (8.7e-5 / 8.8e-5).

**GPU cost** (WebGPU, `gpu_time.mjs --uncapped --rounds 3`, whole-frame GPU ms/frame of the edge scene; adapter
asserted nvidia/lovelace/48 on every run). At a real **2560×1440** canvas (no DOF: 0.11 ms):

| | upstream (32 px) | opt-in 3% (43 px) | opt-in 10% (144 px) |
|---|---|---|---|
| Hexagon Low (default) | 0.64 | 0.61 | 1.33 |
| Hexagon High | 3.20 | 3.20 | 3.37 |
| Circle Medium | 7.83 | 13.91 | (6%: no result within 300 s) |

At 1152×648 (reach 19 / 39 / 65 px at 3 / 6 / 10%): Circle Medium 1.27 (32 px), 0.24, 2.59, 6.17; Circle High 0.72
(3%), 3.55 (6%); hexagon flat (0.10–0.11 Low, 0.32–0.54 High). The Circle grows with the square of the reach, so above
1080p the 3% default costs it more than upstream (1.8× at 1440p). Shipped: 3% default with the project setting; a
larger blur is clamped visibly (question `dof-physical-max-blur` in for-tasks.md).

**Builds:** editor (standard flags) and **dev_mode=yes** editor: rc 0, 0 warnings (`-Werror`), and `--doctool .` changes nothing but the ordering of the new RS method (applied). Web `template_debug` and
**`template_release`** (`dlink_enabled=no threads=no gdscript_braces=yes module_raycast_enabled=yes`): both rc 0,
0 warnings. pre-commit clean on all changed files.

## Skipped (lead's instruction 2026-10-28: only this task's verification)

- smoke.sh: no shader interface changed (no binding; the push constant layout is the same; only a new flag bit and
  shader code), so `interface_reference.json` / `scan_reference.txt` are untouched. The changed bokeh WGSL compiled and
  ran in Chrome through the web exports above (baked by the export).
- Forward+ matrix (dof arms, --parity), scene smoketest, perf_guard, Firefox, other tiers. The user runs the full suite.
- Not tested: XR/multiview, MSAA, FSR 2 (the code takes `base_texture_size` either way), anamorphic beyond one
  render (squeeze 2: horizontal radius 1.92 px vs 1.62 modeled), tilt-shift with the opt-in.

## For the lead

- Merge: shared files touched in their own blocks (`rendering_server*.{h,cpp}` one line each after
  `camera_attributes_set_dof_blur`, storage `/* Physical DOF blur size */`, bokeh files). Task 62 touches Camera3D /
  CameraFinish, no overlap expected.
- Re-run after merge: the bit-identical pair (the `tools/render.sh` cases above), one web template_release.
- Risk: the flag-gated gather change alters the Box/Hexagon look of opt-in cameras only. Off is pixel-identical.
- A detached worktree `../godotwebgpu-63-base` (base commit, for the baseline web template) can be removed.

## Files

`tools/`: dof_test.gd (native scene), render.sh, edge.py, model.py, edge_energy.py, ab63.gd (camera-sim A/B harness,
for a scratch copy whose `update_dof()` returns early for a Physical camera), shoot63.mjs + cases_web.json (web
capture). `camsim_ab_faces.png`: rows Practical-hexagon, Physical-hexagon, Practical-circle, Physical-circle;
columns f/8 u5, f/1.4 u5, f/1.4 u2.5.
