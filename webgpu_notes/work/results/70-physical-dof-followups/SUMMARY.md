# Task 70: physical DOF follow-ups (6% max blur, legacy gather): summary

## What changed

- **`rendering/camera/depth_of_field/depth_of_field_physical_max_blur_size` defaults to 6%** (was 3%; user answer
  `dof-physical-max-blur`). It only affects cameras with `CameraAttributesPhysical.dof_physical_blur_size` on.
- **`CameraAttributesPhysical.dof_legacy_gather`** (bool, default `false`, group Depth of Field, right after
  `dof_physical_blur_size`, and hidden in the inspector while that is off, when the gather is upstream's anyway; user
  answer `camera-sim-hexagon`). The blur keeps the thin-lens size but goes through upstream's gather exactly: a 32 px
  reach whatever the resolution or project setting, and no `BOKEH_PHYSICAL_SIZE` bit (Box/Hexagon divide by every
  sample, §3 trap 97; the Very Low/Low half-size passes blur twice as wide). New RS method
  `camera_attributes_set_dof_legacy_gather(rid, enable)` (its own method, so task 63's RS signature is unchanged),
  storage field `dof_legacy_gather` + getter in the `/* Physical DOF blur size */` block. In
  `BokehDOF::_physical_blur_size()` it returns before setting the reach, and the function now returns "gather sized to
  the blur" (callers' `physical_size` renamed `sized_gather`). No shader change, no binding or push-constant change.
- Docs: `CameraAttributesPhysical.xml` (new member, says it is not physically correct; Circle cost note on
  `dof_physical_blur_size`), `ProjectSettings.xml` (6% rationale, Box/Hexagon coarseness at Very Low, Circle cost),
  `RenderingServer.xml` (new method). `--doctool .` changed only the 3.0 → 6.0 default.

## Verification

Native: Vulkan, RTX 4080 SUPER, task 63's edge scene (`tools/dof_test.gd`, `edge.py`, `model.py`). The pre-change
editor is this branch's base (1ff3d995b5), built in this worktree before any edit.

**Opt-in off and defaults bit-identical** (`tools/bitident.sh`), pre-change vs new editor, 14/14 maxdiff 0: Forward+
and Mobile × {physical hexagon Low, physical circle Medium, physical box High near field, practical hexagon (positive
transitions), practical circle in physical mode, opt-in hexagon Low at maxblur=3, opt-in circle long lens at
maxblur=3}. **Web** (Chrome headed, Vulkan, adapter nvidia/lovelace/48 asserted on every run): a baseline template
built at the base commit in a detached worktree `../godotwebgpu-70-base` + the pre-change editor, against the new
pair, 4/4 maxdiff 0 (physical hexagon Low, physical circle Medium, practical hexagon, opt-in hexagon at maxblur=3).

**6% unclamped where 3% clamped:** 200 mm f/2.8, focus 5 m, wall 50 m, 2560×1440, circle Medium (model radius
80.4 px; cap 43.2 px at 3%, 86.4 at 6%): pre-change default 42.76 (F+) / 43.06 (Mobile), new default **79.53 / 79.99**;
hexagon Medium 10-90% width 112.3 (model 111.3). Web (1280×720 canvas, rendered at 2560×1440, see below): radius 39.79
canvas px against 40.18 modeled (3% would cap at 21.6).

**Legacy reproduces upstream's look for the same size** (`tools/legacy.sh`, `matched.py`). The reference is the
pre-change editor drawing the same thin-lens size through upstream's physical-mode path, the way camera-sim does
before task 63 (`CameraAttributesPractical`, transitions −1, `dof_blur_amount` = the engine's float32 radius / 64,
same vertical FOV). The opt-in-off path is pixel-identical to pre-63 (task 63, re-checked above), so this is a pre-63
render. **20/20 maxdiff 0**, Forward+ and Mobile × {hexagon Very Low/Low/Medium/High, box Low/High, circle Medium,
hexagon near field (u 8, d 3), hexagon at 1440p, long lens circle at 1440p clamped at 32 px}. Web: 4/4 maxdiff 0
(hexagon Low/Medium/High, box High) against the pre-change web template through the same matched settings.
Widths (F+, 720p, 50 mm f/1.4 u2.5 d5, model 7.57 hexagon), Very Low/Low/Medium/High: legacy 8.10 / 7.39 / 0.93 /
0.93 (= pre); exact at the new default 2.18 / 7.75 / 6.57 / 7.06.

**WebGPU cost** (`tools/perf.sh`, `perf1440.txt`; `gpu_time.mjs --uncapped --rounds 3`, with the project viewport and
canvas at 2560×1440, checked before each run per trap 98; whole-frame GPU ms/frame):

| | upstream (32 px) | opt-in 3% (43 px) | **opt-in 6% (86 px)** | legacy |
|---|---|---|---|---|
| Hexagon Low | 0.54 | 0.35 | **0.42** | 0.57 |
| Hexagon High | 1.42 | 1.50 | **1.54** | (= upstream) |
| Circle Medium | 3.97 | 7.83 | **34.74** | 3.98 |

**Why task 63 got no Circle number at 6%:** it is not a hang. Circle cost grows with the square of the reach
(34.7 / 7.8 = 4.4 ≈ (86/43)²), and `gpu_time.mjs` measures 2048 command buffers (≈1024 frames) per round with a fixed
60 s `waitForFunction` timeout. At task 63's rates (its absolute numbers are ~2× these on every row, e.g. Circle
upstream 7.83 against 3.97) a frame took ~70 ms, so a round needed ~72 s and timed out. Still, 34.7 ms per frame on a
4080 SUPER at 1440p (29 fps from DOF alone) is pathologically slow for real time, so the class docs and the setting's
description say so and point to Box/Hexagon, a lower Circle quality or a smaller value.

**New finding (documented; question below):** at 6% the Box/Hexagon samples are spread twice as far apart, so each
quality draws a small blur about as the next lower one did at 3%. At 720p, for a 5.5 px blur, hexagon widths at 3% / 6%
(model 7.57): F+ Very Low 7.75 / **2.18**, Low 7.66 / 7.75, Medium 7.06 / 6.57, High 7.45 / 7.06; Mobile Very Low
6.95 / 3.42; box Very Low 8.33 / 2.18. Very Low (6 steps over half size) misses any blur under ~1% of the image height.

**A web trap that cost an hour (§3 candidate):** the web test project (copied from task 63) sets a 2560×1440 viewport,
and the page is 1280×720, so the engine renders at 2560×1440 and the canvas shows it downscaled. A frame-relative
blur doubles in buffer pixels and a fixed-pixel reference does not, so a "size-matched" comparison computed for 720p
differed (maxdiff 28). Found with a temporary push-constant print (removed, template rebuilt clean). Matched for 1440
it is 4/4 maxdiff 0.

**Builds:** editor (standard flags) and **dev_mode=yes** editor: rc 0, 0 warnings. Web `template_debug` and
**`template_release`** (`dlink_enabled=no threads=no gdscript_braces=yes module_raycast_enabled=yes`): rc 0,
0 warnings each. pre-commit clean.

## Skipped (user: related tests only)

- smoke.sh: no shader, binding or push-constant change, so the interface references are untouched.
- Forward+ matrix, scene smoketest, perf_guard, Firefox, other tiers. Not tested: XR, MSAA, FSR 2 with the legacy
  option (it only changes the reach and one flag bit before any of that).

## For the lead

- Merge: own blocks in shared files (RS: one line each after `camera_attributes_set_dof_physical_blur_size`; storage
  in the task 63 block; the camera attributes property after `dof_physical_blur_size`).
- Re-run after merge: `tools/bitident.sh` and `tools/legacy.sh` (their paths point at this worktree and the scratch
  pre-change editor; substitute the merged editor and a pre-merge copy), plus one web template_release.
- Risk: the 6% default changes how opt-in cameras look: bigger blurs no longer clamp, Very Low Box/Hexagon small blurs
  go sharp, and Circle costs ~4.4× what it did at 3%. Non-opt-in cameras are unaffected (bit-identical).
- `../godotwebgpu-70-base` (detached, base commit, baseline web template) can be removed.

## Files

`tools/`: bitident.sh, legacy.sh, matched.py (size-matched Practical settings), perf.sh, render.sh, export.sh,
canvas_size.mjs, shoot63.mjs, edge.py, model.py, dof_test.gd and main.gd (both take `legacy=1`), cases_*.json.
`perf1440.txt`: raw gpu_time output.
