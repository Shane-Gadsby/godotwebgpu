# 29-white-balance-units: SUMMARY

State: ready-for-integration (branch `work/29`, worker-29, 2026-10-26)

## What changed

User decisions (2026-10-26): Planckian everywhere (unchanged), range 2000-10000 K, tint -150..150 with the physical
extent kept (±150 = the old ±1 = ±0.05 Duv, so one unit is 1/3000 Duv; positive still turns the image magenta).

- `servers/rendering/storage/camera_attributes_storage.cpp`: new `WHITE_BALANCE_DUV_PER_TINT = 0.05 / 150.0`, used where
  the isotherm offset is computed; the clamp in `white_balance_compute_gains()` is now 2000-10000 K and -150..150; the
  model comment says so.
- `scene/resources/camera_attributes.cpp`: inspector hints `2000,10000,1,suffix:K` and `-150,150,1`.
- `doc/classes/CameraAttributes.xml`, `RenderingServer.xml`: the ranges, 1/3000 Duv per unit, and that values outside
  them are clamped (by the RenderingServer, as before; the property stores what it is given).
- Forward+ arm `white_balance` (`fp_scene.gd`): tint 0.2 -> 30; comment in `features.mjs`.
- Task 24's `wb_check` stays as a tool, in the new units: `model.py` (scale `0.05/150`), `predict.py` (adds 3200/30),
  `project/wb.gd` (±0.5 -> ±75, adds 3200/30). pre-commit made them ruff/codespell clean (formatting, gray/center,
  `predict.py`'s star import made explicit); `predict.py` prints the same values as before.
- `for-docs.md`: TASKS.md §4.42 line changes and the full revised wiki page `Engine-Additions-White-Balance.md`. Nothing
  else in the shared docs names the ranges.

## Verification (actual numbers)

Baseline: worker-24's editor (`../godotwebgpu-24/bin`, old units), against this branch's editor
(`scons platform=linuxbsd target=editor webgpu=yes warnings=extra werror=yes`, clean). Native lavapipe/llvmpipe under
xvfb, wb_check frames (240x240) compared pixel by pixel:

| old | new | Forward+ | Mobile | Compatibility |
|---|---|---|---|---|
| 6500 K / 0 | 6500 K / 0 | 0 of 57,600 px differ | 0 | 0 |
| 3200 K / 0.2 | 3200 K / 30 | 0 | 0 | 0 |
| 6500 K / +0.5 | 6500 K / +75 | 0 | 0 | 0 |
| 6500 K / -0.5 | 6500 K / -75 | 0 | 0 | 0 |

- fp_check fixture, `white_balance` arm native sweep (`--fp-only=white_balance`, 640x360), old editor + tint 0.2 against
  new editor + tint 30: off and on frames **0 of 230,400 px differ** each; off->on delta 48.13 (unchanged).
- Clamp (Forward+): 15000 K renders identical to 10000 K, 1000 K to 2000 K, tint ±300 to ±150 (0 px differ each).
- `wb_check` card/patch values match `predict.py` within 1/255 (3200/30: card 126 190 255, patch 211 193 255).
- `run_forward_plus.mjs --parity --feature white_balance` (headed system Chrome, real GPU; `template_release`
  nothreads non-dlink built from this commit): **unbaked PASS**, delta 48.141, native diff off 0.002 on 0.004;
  **baked PASS** (442 baked shaders), same numbers.
- smoke.sh:
  ```
  [WGSL Precompile] Results: 276 compiled, 0 glsl failures, 0 conversion failures
  identical interface: 276/276, mismatched: 0, missing in candidate: 0, not in reference: 0
  against the stored reference: 552/552 identical, 0 differ, 0 missing, 0 not in reference
  validation: 276 modules, 0 invalid, 0 of them expected
  SMOKE OK (smoke-20261026-193126-639717)
  ```

## What the lead must still run

- Rebuild editor and templates together from the merged tree (C++ outside the shaders only).
- `smoke.sh` and `run_forward_plus.mjs --parity --feature white_balance` (unbaked and `--baked`) on the merge.
- Fold `for-docs.md` into TASKS.md §4.42 and the wiki page.

## Risks

None known. Tint values saved in the old -1..1 units would now be 150x weaker, but those units were never released.
`--doctool` was not rerun (text-only doc edits, no member or signature changes).
