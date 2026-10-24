# 01-fsr2-barrier-uniformity: FSR2's luminance pyramid fails WGSL barrier uniformity

State: ready-for-integration
Owner:
Claimed:
Needs: — (tint_convert_cli + Chrome compile; no lease)
Branch: work/1
Results: webgpu_notes/work/results/01-fsr2-barrier-uniformity/

## Context

`servers/rendering/renderer_rd/shaders/effects/fsr2/fsr2_compute_luminance_pyramid_pass.glsl` (variant
`no_image_atomics`, compute) converted by SPIRV-Cross is rejected by Chrome: "'workgroupBarrier' must only be called from
uniform control flow", reached through `SpdDownsampleNextFour` from `SpdDownsample` (TASKS.md §2.35). Barrier
uniformity has no diagnostic switch. Tint's WGSL of the same preprocessed SPIR-V passes, so the difference is in how
SPIRV-Cross emits that control flow (compare the two: `bin/tint_convert_cli.tint` vs `bin/tint_convert_cli`, see
`smoke.sh`). FSR2 is one of the Forward+ features the parity matrix exercises.

## Goal

Patch `thirdparty/spirv-cross-wgsl` (never upstream) so the module is valid WGSL with the same semantics, in general
form (the construct, not the file name), with a reference test shader of our own if useful. Record the patch as
`thirdparty/spirv-cross-wgsl/patches/0001-<name>.patch` (git diff of the vendored files against the pristine
`bad70810` copy) and give the lead the `thirdparty/README.md` line in `for-docs.md`. Remove the file's line from
`webgpu_notes/work/smoke_expected_invalid.txt`.

## Verification

`smoke.sh` passes with an empty expected-invalid list and still 276/276 identical interface. Diff of every other
module's WGSL before/after the patch reported in SUMMARY.md (ideally unchanged). FSR2 rendering is checked by the
lead in the Forward+ parity run (task 07).
