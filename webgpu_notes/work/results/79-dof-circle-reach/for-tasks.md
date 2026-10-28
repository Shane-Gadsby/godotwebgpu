# For TASKS.md (work task 79)

## §4 entry

### 4.xx Physical DOF: Circle reach 3% (October 2026) *(work task 79; user answer `dof-circle-6pct`)*

With `CameraAttributesPhysical.dof_physical_blur_size`, the Circle shape and custom apertures (`aperture_enabled`,
drawn by the circular gather) now reach at most **3%** of the image height, while Box and Hexagon keep
`depth_of_field_physical_max_blur_size` (6% default). A **fixed cap**, `min(setting, 3%)`, not half the setting: the
circle's cost is the square of its absolute reach, so a fixed cap pins it (Circle Medium 7.8 ms at 1440p on WebGPU)
whatever the setting, while half the setting would grow it again whenever a project raises the setting for Box/Hexagon;
a setting under 3% still lowers it. `BokehDOF::_physical_circle_reach()` (`PHYSICAL_CIRCLE_MAX_BLUR_PERCENT`), applied
in the compute (Forward+) and raster (Mobile) paths after the shape is resolved; `dof_legacy_gather` keeps its fixed
32 px. No shader, binding or push-constant change. Docs: the project setting, `dof_physical_blur_size`,
`aperture_enabled`. Worker (pre-change editor = base f3c673deb2): 200 mm f/2.8 at 1440p (model 80.4 px), Circle Medium
79.53 → **42.76** px (F+), 79.99 → 43.06 (Mobile), = the pre-change editor at maxblur 3 exactly (maxdiff 0); custom
6-blade aperture 72.3 → 42.8; Box Medium, Hexagon Very Low/Medium, Circle at maxblur 2 and 3, legacy Circle and Hexagon:
pre vs new maxdiff 0 (Forward+ and Mobile, 14/14); opt-in off/defaults 14/14 maxdiff 0. WebGPU 1440p (`gpu_time.mjs`,
canvas 2560x1440 checked): **Circle Medium 34.74 → 7.85 ms** (7.84 at maxblur 3), custom aperture 8.38, Hexagon Low
0.38, legacy Circle 3.97. Builds: editor, dev_mode editor, web template_debug and template_release, 0 warnings.
Lead (merged tree): ...

## §3 trap candidates

- **`git stash` is shared by every worktree** (2026-10-29, task 79): `refs/stash` lives in the common repository, so
  `git stash` + `git stash pop` in two worktrees a minute apart swaps the two workers' changes (worker-79 popped
  worker-80's stash and worker-80 popped worker-79's). Nothing was lost: worker-79 restored its own changes from the
  stash commit and put worker-80's back with `git stash store 641d394a7d`; worker-80's tree held worker-79's diff until
  it reverts it. Rule for the board: workers never `git stash`; to build a pre-change binary, build before editing or
  use a detached `git worktree add`.

## Questions for the user

None. (The cap is fixed at 3%, as answered; if a project ever wants a larger Circle, a separate setting is the way,
not raising the shared one.)
