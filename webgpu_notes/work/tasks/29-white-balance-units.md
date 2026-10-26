# 29-white-balance-units: white balance range 2000-10000 K and tint -150..150 (user decisions)

State: claimed
Owner: worker-29
Claimed: 2026-10-26 18:40
Needs: build, gpu (the white_balance parity arm)
Branch: work/29
Results: webgpu_notes/work/results/29-white-balance-units/

## Context

Task 24 (TASKS.md §4.42) shipped `CameraAttributes.white_balance_temperature` (1000-15000 K) and `white_balance_tint`
(-1..1 at 0.05 Duv per unit, positive = magenta). User decisions, 2026-10-26, on the hand-offs page: model **Planckian
everywhere** (unchanged), range **2000-10000 K**, tint **-150..150** (Lightroom-style numbers).

## Goal

1. Temperature range 2000-10000 K: inspector hint, the RS setter's clamp and the doc XML.
2. Tint -150..150, **keeping the physical extent**: ±150 is the old ±1, i.e. ±0.05 Duv, so one unit is 1/3000 Duv;
   positive still turns the image magenta. Change the scale constant where the gains are computed
   (`camera_attributes_storage.cpp`), the hint, the clamp, the docs. Values from the old range were never released.
3. The Forward+ `white_balance` arm (`fp_scene.gd`: 3200 K, tint 0.2) becomes tint 30, so its frame is identical; the
   task 24 checker (`results/24-white-balance/wb_check`) updated the same way if it is kept as a tool.
4. Text for the shared docs in `for-docs.md`: TASKS.md §4.42's model and decision lines, and the wiki page
   `Engine-Additions-White-Balance.md` (the lead edits both).

## Verification

Native: 6500 K / 0 bit-identical (unchanged); 3200 K tint 30 identical to the old 3200 K tint 0.2 (pixel diff 0).
`run_forward_plus.mjs --parity --feature white_balance`, unbaked and baked; `webgpu_notes/work/smoke.sh`.
