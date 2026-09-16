# Resolution Plan — Remaining Work Only (Updated 2026-09-16)

Based on `webgpu_notes/TASKS.md`. **Everything already verified `DONE` has been removed** — for the full history of completed work (round-by-round investigation trails, exact fixes, commit hashes), see `webgpu_notes/TASKS.md` and `git log`. This doc only tracks what's still open, ordered **easiest to hardest**.

**Repo state**: on `webgpu-4.7.2`. The SDFGI brightness-runaway bug — the single blocker for a public build — is **fixed and live-verified end-to-end** (Task 9.5 Round 46: root-caused to a shadow-texture-refresh copy using `depth` instead of `layers` for a multi-layer storage texture in `drivers/webgpu/rendering_device_driver_webgpu.cpp`; fixed, confirmed against a from-scratch repro, and confirmed against a 300-second live capture of the real reported scene with zero brightness drift). There is no longer a hard blocker on this project; everything below is incremental hardening.

---

## 1. Fix stale claims in `drivers/webgpu/README.md`'s "Known Limitations" section

**Effort: minutes.** The driver-local README still lists limitations that are no longer true:
- "No subgroup operations" — contradicted by Task 9.4 (subgroups are precompiled and requested as a device feature).
- "Mobile renderer auto-selected" — contradicted by all of Task 9.5 (Forward+ has worked for many rounds now, including SDFGI as of Round 46).

Read the file, cross-check each claimed limitation against the current driver code and `webgpu_notes/TASKS.md`, and correct or remove stale ones. Pure documentation fix, no code/testing required.

---

## 2. CI: commit `screenshot-comparison` baselines instead of regenerating them every run

**Effort: hours.** `.github/workflows/webgpu_tests.yml`'s `screenshot-comparison` job currently runs `run_tests.mjs --update-baselines`, which means it always overwrites and never actually diffs against a prior baseline — so it can never catch a visual regression, only produce a fresh set of "current" screenshots each run. Fix: run it once to generate baselines, commit them under `webgpu_tests/screenshot_comparison/`, then drop `--update-baselines` from the workflow so subsequent runs diff against the committed set and fail on unexpected drift.

---

## 3. CI: verify `wgsl_precompile.py`'s WGSL output is byte-for-byte reproducible across runs

**Effort: ~1 day.** The existing `validate-spirv` CI job proves every real engine shader *converts successfully* through a freshly-built `tint_convert_cli`, but nothing diffs the actual *precompiled WGSL text* (`drivers/webgpu/wgsl_precompiled.gen.h`) across two runs of the same commit — so a nondeterminism bug in the preprocessing passes or Tint itself (e.g. iteration-order-dependent output, uninitialized-memory-dependent text) could silently ship different shader code between builds without any test catching it. Add a CI step (or a `webgpu_tests/` script) that runs `wgsl_precompile.py` twice back-to-back and diffs the generated header, failing on any difference.

---

## 4. Task 9.16 — Linux benchmark suite runner

**Effort: 1-2 days.** `webgpu_tests/benchmark/run_benchmark.sh` is hardcoded to macOS (macOS binary path, `open -a` for launching Chrome, BSD-flavored `sed`). Port it to Linux:
1. Parameterize the Godot binary path and browser-launch command (Linux Chrome is a plain binary invocation, no `open -a` equivalent needed).
2. Swap any BSD `sed`/other macOS-only shell idioms for GNU-compatible ones (or portable `awk`).
3. Get the two known-safe legs working first — WebGL and WebGPU/Forward Mobile — and confirm output format/metrics match the existing macOS results structure.
4. **Treat a third Forward+ leg as a separate follow-on, not part of this task's estimate** — it's unproven on this runner and could surface new bugs unrelated to the port itself; land the Linux port for the two known legs, then decide separately whether to extend it.

---

## 5. Eliminate the runtime Tint-conversion fallback for specialization-constant variants

**Effort: open-ended, likely the largest remaining item — budget multiple days.** This is a real performance/coverage gap, not a correctness bug blocking anything today, but it's the last major architectural loose end in the driver.

**What's already known** (from Task 9.5's Round 20-23 SDFGI investigation and prior direct code reading):

- Godot's specialization constants (`RenderingDeviceCommons::PipelineSpecializationConstant`) are always simple scalars — `BOOL`, `INT`, or `FLOAT` (`servers/rendering/rendering_device_commons.h` ~L680-682) — exactly what WGSL's `override` mechanism is designed to substitute at pipeline-creation time.
- The driver already has a complete, working implementation of exactly that: pipeline-creation code (`rendering_device_driver_webgpu.cpp` ~L9238-9270, mirrored for compute ~L9945-9989) builds `WGPUConstantEntry` arrays from `p_specialization_constants` and passes them to Dawn's native pipeline-constants API — its own comment states the intent directly: *"pipeline constants instead of creating specialized shader modules. This eliminates all runtime SPIR-V patching and Tint conversion."* This path (`use_override_path`) reuses one precompiled base WGSL module and substitutes values at pipeline-creation time, with **zero runtime Tint conversion needed** regardless of how many distinct value combinations exist.
- `use_override_path` is gated on `shader->has_override_declarations`, set by scanning the base module's own Tint-generated WGSL for `@id(N) override` declarations. **This is always false in practice**: `freeze_spec_constant_ops` — the very first of the SPIR-V preprocessing passes, run unconditionally on every shader's base module — evaluates every `OpSpecConstantOp`/`OpSpecConstant*` to its literal default value and strips the `SpecId` decoration *before Tint's SPIR-V reader ever sees the bytes*. There is nothing left for Tint to represent as a WGSL `override` by the time it runs.
- Because of this, **every pipeline that ever needs a non-default specialization value unconditionally takes the "legacy path" instead**: `_create_module_with_spec_constants()` re-patches the shader's *original, pre-preprocessing* SPIR-V with the real values baked in as literal constants, then reruns the *entire* preprocessing pipeline and a fresh Tint conversion, at runtime, once per distinct (shader, value-combination) the engine ever actually requests. This is the actual "runtime Tint fallback" this item wants to close.
- This legacy path is not just a performance/coverage gap — it has at least one documented, still-open **correctness bug** riding along: it doesn't correctly duplicate every WGSL-text-level fixup the primary module gets (the RW-storage-texture split in particular — `webgpu_notes/TASKS.md`'s Round 20-23 SDFGI trail, "root-caused but not landed," reverted and left open specifically because the bind-group/BindGroupLayout plumbing for a split discovered only during specialization was never built).

**Investigation/implementation plan**:

1. **Confirm why `freeze_spec_constant_ops` evaluates everything unconditionally**, before assuming it's safe to change. Check its git history/original commit message and any referenced task in `webgpu_notes/TASKS.md` for the reason it was written this way — it may be an intentional, general simplification rather than a hard requirement. Specifically check whether Tint's SPIR-V reader has any known gaps handling `OpSpecConstantOp` (computed expressions over spec constants, as opposed to a bare `OpSpecConstant`) — if some *forms* of Godot's spec-constant usage genuinely can't survive to become a WGSL `override` (e.g. one that feeds a compile-time-constant expression Tint would need to fold), the fix will need to distinguish "this constant ID is safe to leave overridable" from "this constant ID's *use* requires compile-time resolution" rather than a blanket toggle.
2. **Prototype the minimal version**: make `freeze_spec_constant_ops` conditionally skip evaluating a spec constant when it is *only* ever consumed as a plain runtime value (i.e., its `OpSpecConstant` result feeds directly into ordinary instructions Tint's WGSL writer already knows how to lower to a real WGSL `override`-backed expression, not into another `OpSpecConstantOp`/type-defining instruction it would need to fold at compile time). Confirm this is expressible as a scoped change to one pass by hand-constructing a tiny SPIR-V module with a genuine unfrozen spec constant and running it through `tint_convert_cli` directly, checking whether Tint's SPIR-V reader correctly turns a surviving `OpSpecConstant` + `SpecId` decoration into a WGSL `@id(N) override` on its own.
3. **Verify the existing `WGPUConstantEntry`/`use_override_path` plumbing actually works** once fed real overridable WGSL — there's no evidence anything in the current shader corpus has ever produced `has_override_declarations == true`, so this path may have latent bugs of its own (untested for years is not the same as working). Write a small standalone test shader with a real spec constant surviving step 2's change, precompile it once, and confirm the pipeline-creation code correctly passes its value via `WGPUConstantEntry` and produces correct output at two different specialization values without any second Tint conversion.
4. **Re-run the full regression suite plus a targeted spec-constant-heavy scene** (SDFGI is the known heaviest user of the legacy path per the Round 20-23 trail) after the change, and specifically re-attempt the abandoned RW-storage-texture-split fix in `_create_module_with_spec_constants()` to see if it's now moot (no longer reachable) rather than something that still needs its own separate fix.
5. **For whatever legacy-path usage remains** (any spec constant step 1/2 determine genuinely can't be made overridable): only *then* extend `wgsl_precompile.py`'s ahead-of-time coverage for those specific cases. Values may be scene/asset-dependent (e.g. light or decal counts), so this likely means identifying a bounded, enumerable subset (e.g. boolean feature toggles with only 2 states) rather than attempting full combinatorial coverage, and documenting explicitly which specialization patterns are structurally excluded from ahead-of-time coverage and why.
6. **Verification for the whole item**: `shader_corpus` and `preprocessing_tests` must stay green throughout (they don't exercise specialization constants directly today — a new fixture may be needed to actually cover this path, since its near-total lack of dedicated test coverage is part of why `has_override_declarations`'s permanent falseness went unnoticed this long); `driver_unit_tests` fully green; native editor + full Emscripten web rebuilds; and a live browser re-test of at least one real specialization-constant-heavy feature (SDFGI is the natural candidate) confirming correct rendering, not just successful pipeline creation.

---

## Suggested order of attack

Items 1-4 are quick, independent, and can be done in any order or in parallel — none blocks another. Item 5 is the only large piece of remaining work; it's fully independent of 1-4 and can be picked up whenever there's a multi-day block of time available, starting with its own step 1 (confirming `freeze_spec_constant_ops`'s constraints) before assuming the override-path fix is safe.
