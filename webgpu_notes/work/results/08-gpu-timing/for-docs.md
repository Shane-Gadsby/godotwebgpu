# For the shared docs (task 08)

No SPIRV-Cross patch from this task (the LUT `var<private>` candidate measured no different; it is kept as
`lut-var-private.candidate.patch` in this folder for tasks 11/12 to try on Firefox and the Razr 60, where Dawn's
backend differs).

## CLAUDE.md "Testing" (perf_guard), optional

`gpu_time.mjs` now logs the adapter on a `[gpu_time] adapter vendor/architecture description, maxSampledTexturesPerShaderStage N`
line, so a timing taken on swiftshader (Forward Mobile) shows in the log. Converter A/B on the GPU:
`webgpu_notes/work/results/08-gpu-timing/compare.sh` + `summarize.mjs` (method in that folder's SUMMARY.md).
