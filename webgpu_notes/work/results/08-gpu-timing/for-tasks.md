# For TASKS.md / the board (task 08)

- §2.35: desktop Chrome GPU time, SPIRV-Cross vs Tint: equal or better (heavy Forward+ 1.43 vs 1.47 ms/frame).
  benchmark_postfx (Forward Mobile) is +2% (0.014 ms/frame). Watch it on Android (task 12).
- §3 trap candidate: **a `webgpu_wgsl_backend=tint` template needs `WGSL_BACKEND=tint` in the environment too.** The
  precompile action runs `tint_cli/build.sh`, which builds whatever `WGSL_BACKEND` says (default spirv-cross) into
  `bin/tint_convert_cli`. Without the variable, a "Tint" template ships a SPIRV-Cross precompiled table. Its
  fingerprint does say `backend:tint`, so nothing notices. Fix it in `drivers/webgpu/SCsub` (pass the backend to
  build.sh) when Tint is not about to be deleted anyway.
- §3 trap candidate: **converter A/B templates must be built at the editor's exact commit.** The baked cache is keyed by
  the version hash, and a template one commit off loads `baked: 0`. Build from a detached worktree at that commit.
- Measurement trap (TASKS.md §3 "Measurement traps"): passes under ~0.06 ms/frame move ±0.01 ms between launches of
  the same export, and about 1 launch in 10 runs in a slow GPU state (half the fps). Use ≥3 launches with the order
  rotated, read medians, and confirm any per-pass gap by swapping the WGSL (`gpu_time.mjs --replace-wgsl`) before
  blaming the converter.

## Questions for the user

None.
