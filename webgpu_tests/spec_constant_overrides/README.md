# Specialization-Constant Override Tests

Covers how a pipeline applies Godot's `PipelineSpecializationConstant`s on
WebGPU: by passing them as WebGPU pipeline constants against `@id(N) override`
declarations in the shader's own WGSL, instead of re-patching the shader's
SPIR-V and re-running the whole SPIR-V→WGSL pipeline once per distinct value
combination at runtime.

```bash
node run_tests.mjs                                   # offline + browser (if available)
WEBGPU_REAL_GPU=1 node run_tests.mjs                 # hardware adapter, not swiftshader
WEBGPU_TEST_BROWSER=/path/to/chrome node run_tests.mjs
```

Needs `bin/tint_convert_cli` (`./drivers/webgpu/tint_cli/build.sh`). Without it
the suite skips and passes, like `shader_corpus` does. The browser half needs
Playwright (`npm install`) plus a WebGPU-capable browser; without one the
offline half still runs and the run says what it skipped.

## What it asserts

**Offline** (`tint_convert_cli` on `shader_corpus`'s fixtures):

- `chained_spec_ops.spv`'s three specialization constants survive as
  `@id(0)`/`@id(1)`/`@id(2)` overrides, and its chained `OpSpecConstantOp`
  values stay override *expressions* rather than being folded to literals.
- `spec_constants.spv` is still frozen to its defaults, because it builds an
  `OpSpecConstantComposite` out of one of them (`vec3(AMBIENT_STRENGTH)`), which
  has no scalar WGSL override equivalent. This is the fallback half of
  `spirv_preprocess::spec_constants_overridable()`: get it wrong and Tint
  raises an internal error on that module instead of converting it.

**In a browser**: builds a compute pipeline from that same WGSL twice — once
with no pipeline constants, once with real values for all three ids — and reads
back what the shader wrote each time. The expected numbers are the fixture's own
arithmetic (`(A+B)`, `*C`, `&0xFF`, `>>2`), so a wrong value points at the
override chain rather than at the test.

Overrides are resolved by the WebGPU implementation at pipeline-creation time,
so a software adapter exercises this mechanism as faithfully as hardware does.
The run prints which adapter it got either way.

## Why this tier exists

`shader_corpus` only checks that a shader *converts*; `preprocessing_tests`
checks the SPIR-V passes. Neither one ever checked that a specialization
constant reaches the GPU with the value a pipeline asked for — which is why
`has_override_declarations` could sit permanently false without anything
failing. See `webgpu_notes/TASKS.md` Task 25.
