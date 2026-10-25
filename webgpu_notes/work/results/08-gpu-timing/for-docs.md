# For the shared docs (task 08)

## thirdparty/README.md, spirv-cross-wgsl patch list

- `0018-lut-arrays-as-private-variables.patch`: an array SPIRV-Cross promotes to a lookup table (a `Private`/`Function`
  array with a constant initializer, never written; `is_used_as_lut`) is emitted as `var<private>`, not module-scope
  `const`, unless that constant is also needed in a constant expression. Restores the form Tint emitted (and that
  `hoist_constant_local_arrays`, TASKS.md §2.30, was written to produce); see results/08-gpu-timing/SUMMARY.md for the
  measured effect.

## CLAUDE.md "Testing" (perf_guard line), optional

`gpu_time.mjs` now logs the adapter (vendor/architecture, maxSampledTexturesPerShaderStage) on a `[gpu_time] adapter`
line, so a timing taken on swiftshader (Forward Mobile) is visible in the log. A converter A/B on the GPU:
`webgpu_notes/work/results/08-gpu-timing/compare.sh` + `summarize.mjs` (see SUMMARY.md "Method").
