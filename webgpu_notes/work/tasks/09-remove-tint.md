# 09-remove-tint: Tint leaves the tree

State: open (blocked on 07 reaching parity and 02, 03, 05, 06 integrated)
Owner:
Claimed:
Needs: build
Branch: work/9
Results: webgpu_notes/work/results/09-remove-tint/

## Goal

Delete `thirdparty/tint`, `tint_wrapper.*`, `tint_ir_transforms.*`, the Tint branches of `tint_cli/build.sh`, SCsub,
fuzzers, `smoke.sh`'s reference step, the temporary backend options, Tint entries in `thirdparty/README.md`, CI
workflow steps, and every comment that describes Tint as the converter (`git grep -i tint`; Chrome's own Tint stays
in comments where that is what is meant). Decide with the lead whether `tint_convert_cli` keeps its name (installed
editors, CI artifacts and the baker look for it) — default: keep it, it is a file name, not a claim.

## Verification

`git grep -il tint` reviewed line by line; all 8 web variants + editor build; `local_ci.sh --dev-mode` green; the lead
repeats task 07's final parity run on the result.
