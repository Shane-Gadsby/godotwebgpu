# Task 79: physical DOF Circle reach 3%: summary

## What changed

- With `CameraAttributesPhysical.dof_physical_blur_size`, the **Circle shape and custom apertures**
  (`CameraAttributes.aperture_enabled`, which always draws through the circular gather) reach at most **3% of the image
  height**; **Box and Hexagon keep** `rendering/camera/depth_of_field/depth_of_field_physical_max_blur_size` (6% default).
- **Fixed cap, `min(setting, 3%)`, not half the setting** (user's answer: 3%). The circle's cost is the square of its
  absolute reach, so a fixed cap pins it (Circle Medium ~7.8 ms at 1440p on WebGPU) whatever the setting; half the
  setting would grow it again as soon as a project raises the setting for Box/Hexagon. A setting under 3% still lowers it.
- Code: `BokehDOF::_physical_circle_reach()` + `PHYSICAL_CIRCLE_MAX_BLUR_PERCENT` (`bokeh_dof.{h,cpp}`), applied in
  `bokeh_dof_compute()` (Forward+) and `bokeh_dof_raster()` (Mobile) after the shape is resolved (so a custom aperture
  with the project shape set to Box/Hexagon is capped too), only when the gather is sized (`dof_legacy_gather` keeps its
  fixed 32 px) and both sides are physical. No shader, binding or push-constant change, so smoke.sh skipped.
- Docs: `ProjectSettings.xml` (the setting: Circle/custom 3% cap and why), `CameraAttributesPhysical.xml`
  (`dof_physical_blur_size`), `CameraAttributes.xml` (`aperture_enabled`: its reach is the circle's).

## Verification

Pre-change editor: the base f3c673deb2 built in this worktree before the edit (`$S/godot_pre79`). Tools in `tools/`
(task 63/70's, repointed to `scratchpad/w79`; `dof_test.gd`/`main.gd` gained `aperture=<blades>`), new `check79.sh`.
Native: Vulkan, RTX 4080 SUPER, 2560x1440, the long-lens edge case (200 mm f/2.8, focus 5 m, wall 50 m: model radius
80.4 px; 3% = 43.2 px, 6% = 86.4 px). Edge radius (G channel, `edge.py`), pre → new (`check.txt`):

| case | Forward+ | Mobile | maxdiff |
|---|---|---|---|
| **Circle Medium, default 6%** | 79.53 → **42.76** | 79.99 → **43.06** | 50 / 51 (intended) |
| **Custom 6-blade aperture (project shape Hexagon)** | 72.32 → **42.76** | 72.79 → **43.06** | 44 / 47 (intended) |
| Circle Medium, maxblur 3 | 42.76 = 42.76 | 43.06 = 43.06 | 0 |
| Circle Medium, maxblur 2 (setting < 3% still lowers) | 28.51 = 28.51 | 28.65 = 28.65 | 0 |
| Hexagon Medium (6%) | 81.90 = | 82.80 = | 0 |
| Hexagon Very Low (6%) | 78.47 = | 78.84 = | 0 |
| Box Medium (6%) | 94.61 = | 95.62 = | 0 |
| Legacy gather, Circle long lens | 31.81 = | 31.76 = | 0 |
| Legacy gather, Hexagon Low (720p-sized blur) | 18.00 = | 18.31 = | 0 |

The new default Circle equals the pre-change editor at maxblur 3 pixel for pixel. **Opt-in off / defaults**
(`bitident.sh`, task 70's 7 cases at 720p, Forward+ and Mobile): **14/14 maxdiff 0** (`ident.txt`).

**WebGPU cost** (`perf.sh`, `perf1440.txt`; `gpu_time.mjs --uncapped --rounds 3`, project viewport and canvas
2560x1440 checked before each run per trap 98, adapter nvidia/lovelace/48 asserted by `canvas_size.mjs`; system
Chrome headed with the standard flags; whole-frame GPU ms):

| | task 70 (6%) | **task 79 (default)** |
|---|---|---|
| Circle Medium | 34.74 | **7.85** (7.84 at maxblur 3; task 70's 3%: 7.83) |
| Custom 6-blade aperture, Medium | (not measured) | 8.38 |
| Hexagon Low | 0.42 | 0.38 |
| Circle Medium, legacy gather | 3.98 | 3.97 |

**Builds** (under the `build` lease, ccache): editor rc 0, **dev_mode=yes** editor rc 0 (`-Werror`, 0 warnings), web
`template_debug` and **`template_release`** (`dlink_enabled=no threads=no gdscript_braces=yes
module_raycast_enabled=yes`) rc 0, 0 warnings. `pre-commit run --files` on the changed files: passed.

## Skipped (user: only this task's checks)

- smoke.sh (no shader interface change), the Forward+ dof arms, Firefox, a web image comparison (the change is CPU-side
  push-constant setup shared by both paths; WebGPU is covered by the cost run).

## Incident (no damage)

`git stash` is repository-wide: worker-79 stashed (to build the pre-change editor), worker-80 stashed a minute later,
and each popped the other's. Worker-79 restored its own diff from its stash commit (`f8a39fa318`) and re-stored
worker-80's (`641d394a7d`) as `stash@{0}`; worker-80's tree then held worker-79's DOF diff (reported to the lead at
once). §3 trap candidate in `for-tasks.md`.

## Risks

- A project that wants a large Circle blur now sees it clamp at 3%; the docs say to use Box/Hexagon. A separate
  setting would be the way to lift it, not the shared one.
