# Resolution Plan — Remaining Work Only (Updated 2026-09-19)

Based on a full re-read of `webgpu_notes/TASKS.md` (through Task 15), `drivers/webgpu/README.md`, `CLAUDE.md`, `.github/workflows/webgpu_tests.yml`, and the current working-tree diff. **Everything already verified `DONE` has been removed** — for the full history of completed work (round-by-round investigation trails, exact fixes, commit hashes), see `webgpu_notes/TASKS.md` and `git log`. This doc only tracks what's still open, ordered **easiest to hardest**.

**Repo state**: on `webgpu-4.7.2`. The SDFGI brightness-runaway bug (the original public-build blocker) is fixed and live-verified. Task 12's `threads=yes` crash for the common case (`dlink_enabled=no`) is fixed and live-verified (commit `6893725f61`); the GDExtension+threads combination (`dlink_enabled=yes threads=yes`) was root-caused to Emscripten-internal dylink/pthread bugs and deliberately left unpatched at the time — **that decision was reversed on 2026-10-07 and the combination now works** on a toolchain carrying the two patches in `misc/emsdk_patches/` (see item 7 below). Task 15's Tint ICE regression from the emsdk upgrade is fixed. There is no hard blocker on this project; everything below is incremental hardening.

**Uncommitted work in the tree right now**: a substantial, largely-working implementation of export-time shader baking (Task 13) — new `drivers/webgpu/spirv_to_wgsl.{h,cpp}`, `drivers/webgpu/wgsl_bake_subprocess.{h,cpp}`, `editor/shader/shader_baker/shader_baker_export_plugin_platform_webgpu.{h,cpp}`, plus changes to `drivers/webgpu/rendering_shader_container_webgpu.*`, `drivers/webgpu/spirv_preprocess.*`, `drivers/webgpu/tint_cli/main.cpp`, `editor/editor_node.cpp`, three platforms' `detect.py`, and `drivers/SCsub`/`drivers/webgpu/SCsub`. It's been live-verified against the user's real `cameraSim` project (real crashes found and fixed along the way — see item 6) but is **not yet committed**, and its own notes record a currently-broken clean `platform=web target=template_release` build (see item 6). Item 6 below covers finishing and landing this.

---

## Completed since this plan was written, not originally tracked here: Task 24 — Anti-aliasing (MSAA 2D/3D, TAA, DOF) `[DONE, 2026-09-20]`

Not one of the 11 items below — surfaced by live user bug reports (real black screens / console error spam / silently-corrupted DOF against their actual `cameraSim` project) after this plan was written, investigated and closed out as its own self-contained arc. Full round-by-round root-cause trail is in `webgpu_notes/TASKS.md` Task 24; summary here for sign-off purposes only.

**11 distinct bugs found and fixed**, spanning 5 rounds of live testing against the user's real project (not just synthetic fixtures) — MSAA sample-count clamping (WebGPU only ever accepts 1 or 4, never 2/8/16/32/64), a `copy.glsl` destination-format gap for TAA's 2-channel velocity buffer, `ClusterDebugShaderRD` building an unused bind group every frame, a completely unimplemented `command_resolve_texture()` (MSAA never actually resolved at all), a depth/float sample-type fallback substituting fake zero depth for DOF, an unclamped core sample-count table feeding the depth-resolve shader more samples than the driver's own clamp provides, an SDFGI/VoxelGI texture-array binding split that silently never worked during export-time baking (compiled with the wrong driver identity), a multisampled-depth reclassification gap causing more zero-depth corruption, a regression in an older depth/float fallback check, a second real DOF-depth fix (Forward+ was missing a guard Forward Mobile already had), and finally a format-converting compute-shader resolve fallback for the one case (TAA's velocity buffer) WebGPU's native resolve mechanism structurally cannot handle.

**Verified**: every originally-reported configuration (all-options, MSAA-2D-only, MSAA-3D-only, TAA-only) *and* the user's actual live project with its full feature set active simultaneously (SSAO+SSIL+SSR+SDFGI+Glow+Fog+VolumetricFog+DOF) — across every MSAA level, MSAA 2D, and TAA combined with MSAA 3D — now show zero console errors and correct rendering (screenshot-confirmed against MSAA-off baselines each time). Full regression suite (`shader_corpus` 13/13, `preprocessing_tests` 199/199+1 skip, `driver_unit_tests` 327/327) stayed green after every fix.

**Commit status**: rounds 1-4 of this work are already committed (`79ce22b0c1` "TAA fixed", `0979f8238a` "Staging updates to the MSAA / TAA / AA / etc systems", `96e8ef446a` "more msaa + taa fixes (corrects formats via a compute shader)"). The final round-5 fix (Bug 11, the format-converting compute-shader resolve) is still uncommitted in the working tree as of this write-up.

---

## 1. ~~Fix stale claims in `drivers/webgpu/README.md`'s "Known Limitations" section~~ — DONE 2026-09-19

Fixed: corrected the "no subgroup operations" claim (nuanced — device feature works internally for built-in shaders, only the user-facing capability query is still 0) and the "mobile renderer auto-selected" claim (Forward+ works, confirmed via `maxSampledTexturesPerShaderStage` request code), and added the `threads=yes` support-matrix note from Task 12. Committed (`902f5c3059`).

---

## 2. ~~CI: commit `screenshot-comparison` baselines instead of regenerating them every run~~ — DONE 2026-09-19

Fixed: ran `node screenshot_tests.mjs --update-baselines` locally (Playwright + Chromium/Firefox already available), verified a clean re-run diffs 8/8 exact-match against them, committed the 8 baseline PNGs, and dropped `--update-baselines` from `.github/workflows/webgpu_tests.yml`. Committed (`902f5c3059`). Note: the cross-browser (Chromium vs. Firefox) comparison reports a very high diff ratio (~99%, `WARN` status, not `FAIL` — outside this item's scope, but worth a look if picked up separately; see cross-browser comparison note under item 9/Task 10.1).

---

## 3. CI: verify `wgsl_precompile.py`'s WGSL output is byte-for-byte reproducible across runs

**Effort: ~1 day.** Still open — no determinism check exists anywhere in the workflow or `webgpu_tests/` today (confirmed via grep). The existing `validate-spirv` CI job proves every real engine shader *converts successfully* through a freshly-built `tint_convert_cli`, but nothing diffs the actual *precompiled WGSL text* (`drivers/webgpu/wgsl_precompiled.gen.h`) across two runs of the same commit — so a nondeterminism bug in the preprocessing passes or Tint itself (e.g. iteration-order-dependent output, uninitialized-memory-dependent text) could silently ship different shader code between builds without any test catching it. Add a CI step (or a `webgpu_tests/` script) that runs `wgsl_precompile.py` twice back-to-back and diffs the generated header, failing on any difference.

This gains extra relevance once item 6 (export-time baking) lands, since `tint_convert_cli --batch` is now invoked from a second call site (the editor's export-time baker) with its own subprocess/threading concerns (Task 13's fork-hazard mutex) — a determinism check here would also catch any nondeterminism the subprocess-isolation path introduces that the old in-process path didn't have.

---

## 4. Task 9.16 — Linux benchmark suite runner — PARTIALLY DONE 2026-09-19

The porting work (original scope) is done and live-verified; the Forward+ leg and README table update remain open as their own follow-on (this was always explicitly out of scope for the port itself — see below).

**Done**: `run_benchmark.sh` now detects the host OS (`uname -s`) and picks the right editor binary, native-backend label, and `sed -i` invocation (BSD vs. GNU) for each. The macOS-hardcoded, hash-pinned clean-WebGL-baseline template path was replaced with an explicit `GODOT_WEBGL_BASELINE_ZIP` env var (clear error + platform-appropriate default-location hint when unset) — there's no reliable way to auto-locate "the official upstream template, not this fork's own WebGPU-contaminated build" across machines. Also fixed a small pre-existing leak (the injected profiler script's `.uid` sidecar was never cleaned up) and gitignored the `exports/` output directory. Committed (`300ae98cd5`).

**Verified**: both known-safe legs run end-to-end on this Linux box against `webgpu_tests/benchmark/godot/scene_a_sprites`, clean state restore on exit for both. `webgpu` leg: 165 fps avg, 2 draw calls. `webgl` leg (once the user installed the official 4.7.2 export templates and `GODOT_WEBGL_BASELINE_ZIP` was pointed at `~/.local/share/godot/export_templates/4.7.2.stable/web_nothreads_release.zip` — confirmed genuinely official via `godot.wasm` size and the in-browser `v4.7.2.stable.official.<hash>` banner, distinct from this fork's `.custom_build.<hash>`): Compatibility renderer, WebGL 2.0, 165 fps avg, 1000 draw calls (higher than WebGPU's 2 — expected, Compatibility doesn't batch these sprites the same way Forward Mobile does). `native` leg is interactive and wasn't run to a natural close in this pass.

**Still open** (not attempted, per this task's original explicit scope split):
1. **The Forward+ leg remains a separate follow-on, not part of this item's estimate** — it's unproven on this runner and could surface new bugs unrelated to the port itself (Task 9.5's own history shows Forward+ under load is where new bugs get found).
2. Update the README's `Key Stats`/`Performance Benchmarks` tables with real, fork-specific Linux numbers across all 7-8 benchmark scenes (only 1 scene has been run on each of the two known-safe legs so far, as a port-verification smoke test, not a full benchmark pass).

---

## 5. Commit the export-time shader-baking work already done, then close its two known open tails `[DONE, 2026-09-25]`

> **Update 2026-09-25**: the baking work landed, and Task 25 (plan item 11) removed the need for the
> specialization-constant half of it entirely — one base module now serves every value combination, so
> export-time container baking is sufficient on its own and is default-on for Web presets. The
> record-then-bake subsystem has been removed. The crash tail in point 1 below is unaffected and still
> open; point 2's clean-build breakage should be re-checked rather than assumed. See Task 13's
> 2026-09-25 update in `webgpu_notes/TASKS.md`.

**Effort: 1-2 days**, mostly finishing/verifying already-written code rather than new design.

**What already exists (uncommitted)**: Task 13's core design — subprocess-isolated export-time WGSL baking via `bin/tint_convert_cli --batch`, wired into `ShaderBakerExportPluginPlatformWebGPU` and `RenderingShaderContainerWebGPU::_set_code_from_spirv()` — is implemented, live-tested against the user's real `cameraSim` project, and has already had three real bugs found and fixed in the process (a Tint ICE in `EmitImageWrite`, a Tint ICE in `EmitImageFetchOrRead`, and a multithreaded-`fork()` hazard in the baking subprocess launcher, all documented in `webgpu_notes/TASKS.md`'s September 18 completion notes). This is real, substantial, verified work sitting uncommitted in the working tree.

**Two things are still genuinely open, not just uncommitted**:
1. **A long tail of distinct Tint/SPIR-V crash classes remains** in the user's real project: 14 of the original 44 "Tint crashed" bake failures are still unidentified crash signatures (`parser.cc:745`/`parser.cc:2493`/`parser.cc:683`/others), plus one newly-surfaced, entirely separate bug (`lang/spirv/reader/lower/atomics.cc:413`, `Switch() matched no cases. Type: tint::core::ir::Phony`) hit live in-browser after the two known fixes landed. Each bug found so far in this project has been real, narrow, and independently fixable — there's no evidence yet that this tail is close to ending, so treat it as open-ended rather than assuming one more pass closes it.
2. **A clean `platform=web target=template_release` build is currently broken** in this working tree — not caused by the baking feature itself, but discovered while re-verifying it: two concurrent `scons` invocations for different platforms raced on a shared, non-platform-suffixed generated file (`modules/register_module_types.gen.cpp`), and a from-scratch rebuild after deleting it now produces undefined-symbol link errors for several modules (`openxr`, `raycast`, `webxr`, `xatlas_unwrap`, `betsy`, `camera`, `cvtt`, `lightmapper_rd`, `objectdb_profiler`) whose per-module static libraries aren't being linked for this exact platform+target combination. This predates this session's baking work but was only just tripped over. The currently-installed web export template (from an earlier successful build this session) still works and isn't affected, so nothing is blocked today — but a genuinely clean build of this target is broken right now.

**Plan**:
1. Review the uncommitted diff as a whole (`git status`/`git diff` on the files listed at the top of this doc), confirm it matches the intent described in `webgpu_notes/TASKS.md`'s completion notes, and commit it (following this fork's per-task commit convention). Do not fold in unrelated changes.
2. Fix the broken clean web build first, since it blocks fully verifying anything else here: rebuild `platform=web target=template_release` from scratch (**one platform at a time — never two concurrent `scons` invocations in this repo**, per the landmine hit this session), and root-cause why `register_module_types.gen.cpp`'s generation disagrees with which per-module `SCsub`s actually ran for `platform=web`. Confirm the fix by getting a fully clean rebuild to link successfully with no missing `uninitialize_*_module()` symbols.
3. Use the new `WEBGPU_BAKE_DEBUG_DUMP=<dir>` env var (already added this session specifically for this purpose) to capture the remaining 14 unidentified crash SPIR-Vs plus the new `atomics.cc:413` Phony-type case, and work through them the same way the first two were resolved: `cuda-gdb` (this machine's only available gdb-compatible debugger) against `tint_convert_cli` for a real backtrace, then either a new `spirv_preprocess` pass or a vendored Tint patch, whichever the root cause calls for.
4. Re-run the full shader corpus and preprocessing test suites after each fix (13/13 and 192/192+1 skip respectively, per the existing baseline) to confirm no regressions.
5. Re-export the user's real project after each round and confirm the bake-failure count keeps dropping without new crash classes appearing; re-verify in-browser that previously-reported crashes stay fixed.
6. Once the crash tail is exhausted or clearly plateaus (diminishing returns on a long-tail bug hunt), update `webgpu_notes/TASKS.md`'s Task 13 status from `TODO` to reflect the real state, and update `CLAUDE.md`'s shader-pipeline description plus `drivers/webgpu/README.md` per Task 13's own subtask 5 (the pipeline is now a three-tier lookup: build-time engine table → export-time project table → runtime fallback, not just build-time-or-runtime).

---

## 6. Task 8.3 — remaining pre-existing (non-regression) Tint conversion failures `[DONE, 2026-10-07]`

> **Closed 2026-10-07.** A forced full precompile sweep reports `274 compiled, 0 glsl
> failures, 0 tint failures`: four of the six are fixed, and the two `tonemap_mobile`
> subpass variants are deliberately excluded by `wgsl_precompile.py` because the engine
> never compiles them on this platform — which is the reachability question this item
> asked. Nothing to add to `expected_failures.json`. Per-shader table and the method
> caveat (`wgsl_precompiled.gen.h` is keyed by SPIR-V hash, so grepping it by name proves
> nothing) are in `webgpu_notes/TASKS.md` Task 8.3's closing note.

**Effort: 1-2 days.** Six shaders were originally identified as failing Tint conversion independent of the 4.7.2 upstream sync. Status per shader, re-checked against `webgpu_notes/TASKS.md`:
- `tonemap.glsl:bicubic{,_1d_lut}:frag` — already fixed as a side effect of Task 8.2's `inline_opaque_functions` pass. No action needed.
- `screen_space_reflection_filter.glsl:default:comp` — root-caused and fixed (missing `layout(rgba16f, ...)` qualifier on the `dest` storage image, matching its sibling shader) but **not yet live-verified against a real build** per the write-up's own note. Do that verification (rebuild, rerun `tint_convert_cli`/`wgsl_precompile.py`, confirm this shader now converts) and close it out.
- `tonemap_mobile.glsl:subpass:frag` / `tonemap_mobile.glsl:subpass_1d_lut:frag` — likely unreachable on WebGPU at all (subpass/input-attachment variants, and WebGPU has no subpass support per this driver's own documented design). Confirm unreachability first; if confirmed, add both to `expected_failures.json` rather than attempting a fix.
- `volumetric_fog.glsl:default:comp` — a Tint crash with the same *signature* as Task 8.2's `ConvertUserCall` bug class but a different trigger (no `area_light_atlas`/`ltc_evaluate` references). Needs the same instrumentation approach used to root-cause Task 8.2 before assuming the same fix applies.
- `voxel_gi_debug.glsl:default:vert` — a `read_write` storage buffer used in the vertex stage, which WGSL disallows but GLSL/Vulkan permits. Needs the buffer split into a read-only vertex-stage view.
- `sdfgi_debug_probes.glsl:default:vert` — the vertex entry point's `position` builtin output isn't surviving the SPIR-V round-trip; needs tracing through the preprocessing passes to find where it's lost.

**Verification**: `shader_corpus` and `preprocessing_tests` stay green; `wgsl_precompile.py`'s real-engine-shader sweep drops its Tint-failure count for each shader fixed, with any confirmed-unreachable variant moved to `expected_failures.json` instead.

---

## 7. Task 12's `threads=yes dlink_enabled=yes` limitation `[DONE, 2026-10-07 — but not as planned]`

**This item asked for the limitation to be documented as permanent. It was instead fixed.** The plan here was to write down "known-unsupported pending an upstream Emscripten fix, not something this fork intends to patch". Task 14 subtask 1.5 then costed patching it properly, found it tractable, and the combination now boots clean on a patched toolchain — so the documentation says the opposite of what this item specified.

What actually landed:
1. Two toolchain patches in `misc/emsdk_patches/` with content-pinned apply/revert tooling: `0001` (the `libdylink.js` `ASM_CONSTS` race this item describes) and `0002` (`-sEXPORT_ALL=1` re-exporting the `HEAP*` views eagerly, which threw on every pthread worker). All four build helpers and both web CI workflows apply them; `platform/web/detect.py` hashes the toolchain into the link so a patch cannot silently fail to take effect.
2. `README.md`, `CLAUDE.md`, `drivers/webgpu/README.md`, `webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md` and `webgpu_tests/README.md` all updated to describe the patched-toolchain requirement rather than an unsupported combination.
3. Verified against a real GDExtension + thread-support project, headless over COOP/COEP: 0 console errors, running its own game scripts.

**Still open from this item**: filing `0002` upstream (the asymmetry between `exportRuntimeSymbols()` and `exportLibrarySymbols()` looks like a plain oversight, so it is a good PR candidate). `0001` is the harder sell and is documented as needing both halves together. Neither patch can self-retire — `apply.sh` refuses when upstream moves, but cannot tell you upstream has fixed the bug; each patch header carries its own "HOW TO TELL WHEN THIS CAN BE DELETED" test.

---

## 8. Task 10.3 — re-triage stale open TODOs (7.8, 7.15, 7.17, 7.18)

**Effort: half a day to re-triage all four; each may spawn its own follow-up fix task if still real.** These four were identified during Phase 7's April 2026 audit, before the Phase 8 upstream sync, all of Phase 9's Forward+ work, and Task 9.15's WGSL-patching architecture changes — each needs a fresh look before trusting its original description.

1. **Task 7.8** (buffer mapping returns stale/zero data, `buffer_map()`/`buffer_unmap()`): re-check whether the described code path still exists as written post-sync, and attempt a fresh minimal repro. Likely a fundamental WebGPU single-threaded-WASM limitation rather than a bug — but confirm no Godot code path actually depends on same-frame readback before closing as "working as intended."
2. **Task 7.15** (`WGUniformSet::temp_views` may leak, `webgpu_objects.h`): check current `uniform_set_free()` for whether it actually releases `temp_views`; if not, characterize whether the leak grows unbounded across frames or is bounded/benign in practice before deciding this needs a fix.
3. **Task 7.17** (specialized shader module cleanup — do `render_pipeline_free()`/`compute_pipeline_free()` release `WGPipelineWrapper::specialized_modules`?): check first whether this is superseded by item 9's spec-constant work below — if the legacy per-value-combination shader-module path goes away, this leak's surface area shrinks or disappears with it, so decide whether to track it here or fold it into item 9.
4. **Task 7.18** (WGSL string remapping fragility, in-place fixed-length `memcpy` on format-name substitution): Task 9.15 already migrated some of this WGSL-text patching to Tint IR-level transforms and explicitly assessed (without migrating) the rest — check whether that investigation already supersedes or narrows this task's remaining concern before doing new work.
5. For each of the four, update its `Status` in `webgpu_notes/TASKS.md` in place (`DONE`/`SKIPPED` with reasoning, `CONFIRMED — still open` with a fresh repro, or superseded/folded into another task) rather than leaving it as a stale `TODO`.

---

## 9. Task 10.1, 10.2, 10.4 — post-compatibility sweep

**Effort: 2-3 days combined**, mostly build/test time plus careful triage; can be sequenced in either order, though 10.1's rebuild is a useful prerequisite for 10.4's live testing.

**10.1 — full local CI run**: `local_ci.sh` mirrors CI but hasn't been run end-to-end (not `--quick`) against current tip, which now includes the SDFGI fix, FSR1/FSR2 work, the Phase 8 upstream sync, Task 12's threads fix, and Task 15's Tint-patch fix. Rebuild native editor and the full web template, confirm `wgsl_precompile.py` reports zero Tint failures against the current baseline, run the full suite, and triage any failure as genuine regression vs. stale fixture/baseline drift — cross-check the screenshot-comparison tier by hand rather than trusting a green run at face value until item 2 above actually fixes it to diff against a real baseline. File any confirmed new bug as its own task rather than fixing inline.

**10.2 — base-class interface audit**: per `CLAUDE.md`'s standing rule, any change to `rendering_device_driver.h`/`rendering_context_driver.h`/`rendering_shader_container.h` requires auditing all pure-virtual overrides in `drivers/webgpu/` for gaps. This hasn't been redone since before the Phase 8 sync and the FSR1/FSR2 work, both of which touched shared renderer code calling into the driver. Diff the three headers against the pre-sync commit, confirm every changed/added method has a real WebGPU override (not a silently-inherited base default), specifically re-check the `API_TRAIT_*` block (now larger — Task 12 added `API_TRAIT_REQUIRES_SYNCHRONOUS_PIPELINE_COMPILATION`) against `api_trait_get()`'s base-class defaults, and record the audit's outcome (clean, with commit range checked, or gaps found as new numbered tasks).

**10.4 — fresh live testing against the real project**: every fixture-based test tier only covers what it was written to cover; the deepest real bugs found in this project (SDFGI, FSR1/2, the `threads=yes` crashes, Task 13's baking crashes) were all found by live-testing the user's actual project. Per user memory, real testing happens against `~/Downloads/cameraSim_.../testing`, not `webgpu_tests/test_project`. Capture native-Vulkan and WebGPU baselines for scenes/features not already exhaustively covered by Task 9.5's rounds or Task 13's export-testing pass, diff visually and numerically for drift (favor long captures for anything accumulator/history-buffer-based, per SDFGI's lesson), and file any confirmed new issue as its own numbered task.

---

## 10. Task 14 — reduce post-download loading blocking, make the progress bar representative `[SUBSTANTIALLY DONE; one named next step]`

> **Where this actually landed (2026-10-07).** Points 1-4 are done: the stall is fully
> decomposed (`webgpu_tests/startup_phases/`, ~2.0 s → ~1.0 s on the user's real project),
> point 2 needed no work (`requestAdapter` starts at 91 ms, already parallel with the WASM
> fetches), point 3 shipped as the startup split into frame-sized steps plus per-phase
> progress to JS, and the glyph-atlas coalescing took ~800 ms off the real project,
> verified in-browser and natively. **The one named next step** is attributing the largest
> remaining term — a Chrome-internal synchronization above a ~20-30 MB upload threshold,
> not our pipeline compilation — which needs a Chrome trace (CDP `Tracing.start` with the
> `gpu` and `disabled-by-default-gpu.debug` categories) rather than more black-box
> bisection. See `webgpu_notes/TASKS.md` Task 14 subtask 2.

**Effort: 1 day, needs a real GPU + browser session.** `platform/web/js/engine/preloader.js`'s `animateProgress()` computes progress purely from bytes downloaded, with no visibility into WASM compile/instantiate time, the async `GPUDevice` request cycle, precompiled-WGSL table setup, or (until item 5 above closes it) any runtime Tint shader-fallback conversions — any of which can produce a visible stall after the bar already reads 100%.

**Depends on item 5** (Task 13) being far enough along that a runtime shader-fallback stall isn't misattributed to something else here — an unclosed export-time-baking gap is itself a source of post-100% blocking this task would otherwise chase in the wrong place.

1. Instrument and quantify each post-download phase (WASM fetch-complete → instantiate-complete, device-request start → resolve, engine `main()` start → first frame) against both a small export and the user's real `cameraSim`-scale project, to find the actual largest contributor rather than guessing.
2. Reduce real blocking time where possible: confirm `WebAssembly.instantiateStreaming` is actually taken (not silently falling back due to MIME-type/header issues), and confirm the JS shell's device pre-initialization is kicked off in parallel with the WASM fetch, not serialized after it.
3. Extend `preloader.js`'s progress model to reserve estimated weight for the post-download phases, wired to real phase-completion signals (WASM instantiated, device acquired, first frame rendered) via `Config.prototype.onProgress`, rather than a fixed-timer guess — and handle a phase taking far longer than expected with a graceful "initializing renderer..." style indicator instead of a bar that freezes or snaps to 100% then hangs.
4. Verify with before/after timing numbers against the user's real project, and visually confirm behavior under both a throttled network (stresses download-progress) and a cold cache (stresses post-download phases).

---

## 11. ~~Eliminate the runtime Tint-conversion fallback for specialization-constant variants~~ — DONE 2026-09-25 (Task 25)

**Landed.** `freeze_spec_constant_ops()` is now conditional on a new
`spirv_preprocess::spec_constants_overridable()`, so specialization constants
normally survive into WGSL as `@id(N) override`s and pipeline creation sets them
with WebGPU pipeline constants — one base module per shader, no runtime
re-patching or re-conversion. All 37 of the 196 real engine shader variants that
declare specialization constants now emit overrides (0 did before, confirming
this item's premise empirically); all 196 still convert; per-stage binding and
sampler counts are byte-identical, so the Task 19/23 sampler-limit risk did not
materialize. A pre-existing mislabelled `CreateAggressiveDCEPass` argument was
found and fixed on the way (spec-constant preservation is an optimizer *option*,
and was never actually set). New test tier
`webgpu_tests/spec_constant_overrides/` proves in a browser that the
`WGPUConstantEntry` plumbing works — the first time it has ever run.

**Still open from this item**: none of it has been through a real engine/browser
run (no Emscripten in the sandbox it was done in), and
`rendering_device_driver_webgpu.cpp` has not been compiled. See Task 25's "Not
verified" list in `webgpu_notes/TASKS.md` for exactly what the first live run
should confirm. Item 10 (Task 14's loading stall) depended on this and is now
unblocked.

The original analysis and plan are kept below for reference.

---

**Effort: open-ended, likely the largest remaining item — budget multiple days.** This is a real performance/coverage gap, not a correctness bug blocking anything today, but it's the last major architectural loose end in the driver. Unchanged from the prior pass — nothing in the last three days' work has touched this path.

**What's already known** (from Task 9.5's Round 20-23 SDFGI investigation and prior direct code reading):

- Godot's specialization constants (`RenderingDeviceCommons::PipelineSpecializationConstant`) are always simple scalars — `BOOL`, `INT`, or `FLOAT` (`servers/rendering/rendering_device_commons.h` ~L680-682) — exactly what WGSL's `override` mechanism is designed to substitute at pipeline-creation time.
- The driver already has a complete, working implementation of exactly that: pipeline-creation code (`rendering_device_driver_webgpu.cpp` ~L9238-9270, mirrored for compute ~L9945-9989) builds `WGPUConstantEntry` arrays from `p_specialization_constants` and passes them to Dawn's native pipeline-constants API — its own comment states the intent directly: *"pipeline constants instead of creating specialized shader modules. This eliminates all runtime SPIR-V patching and Tint conversion."* This path (`use_override_path`) reuses one precompiled base WGSL module and substitutes values at pipeline-creation time, with **zero runtime Tint conversion needed** regardless of how many distinct value combinations exist.
- `use_override_path` is gated on `shader->has_override_declarations`, set by scanning the base module's own Tint-generated WGSL for `@id(N) override` declarations. **This is always false in practice**: `freeze_spec_constant_ops` — the very first of the SPIR-V preprocessing passes, run unconditionally on every shader's base module — evaluates every `OpSpecConstantOp`/`OpSpecConstant*` to its literal default value and strips the `SpecId` decoration *before Tint's SPIR-V reader ever sees the bytes*. There is nothing left for Tint to represent as a WGSL `override` by the time it runs.
- Because of this, **every pipeline that ever needs a non-default specialization value unconditionally takes the "legacy path" instead**: `_create_module_with_spec_constants()` re-patches the shader's *original, pre-preprocessing* SPIR-V with the real values baked in as literal constants, then reruns the *entire* preprocessing pipeline and a fresh Tint conversion, at runtime, once per distinct (shader, value-combination) the engine ever actually requests. This is the actual "runtime Tint fallback" this item wants to close.
- This legacy path is not just a performance/coverage gap — it has at least one documented, still-open **correctness bug** riding along: it doesn't correctly duplicate every WGSL-text-level fixup the primary module gets (the RW-storage-texture split in particular — `webgpu_notes/TASKS.md`'s Round 20-23 SDFGI trail, "root-caused but not landed," reverted and left open specifically because the bind-group/BindGroupLayout plumbing for a split discovered only during specialization was never built).
- **Possibly related**: Task 10.3 (item 8 above) flags that Task 7.17's specialized-shader-module cleanup TODO may be superseded by whatever this item does to the legacy path — check that overlap as part of item 8, not here, but keep it in mind since fixing this item may make 7.17 moot rather than something that needs its own separate fix.

**Investigation/implementation plan**:

1. **Confirm why `freeze_spec_constant_ops` evaluates everything unconditionally**, before assuming it's safe to change. Check its git history/original commit message and any referenced task in `webgpu_notes/TASKS.md` for the reason it was written this way — it may be an intentional, general simplification rather than a hard requirement. Specifically check whether Tint's SPIR-V reader has any known gaps handling `OpSpecConstantOp` (computed expressions over spec constants, as opposed to a bare `OpSpecConstant`) — if some *forms* of Godot's spec-constant usage genuinely can't survive to become a WGSL `override` (e.g. one that feeds a compile-time-constant expression Tint would need to fold), the fix will need to distinguish "this constant ID is safe to leave overridable" from "this constant ID's *use* requires compile-time resolution" rather than a blanket toggle.
2. **Prototype the minimal version**: make `freeze_spec_constant_ops` conditionally skip evaluating a spec constant when it is *only* ever consumed as a plain runtime value (i.e., its `OpSpecConstant` result feeds directly into ordinary instructions Tint's WGSL writer already knows how to lower to a real WGSL `override`-backed expression, not into another `OpSpecConstantOp`/type-defining instruction it would need to fold at compile time). Confirm this is expressible as a scoped change to one pass by hand-constructing a tiny SPIR-V module with a genuine unfrozen spec constant and running it through `tint_convert_cli` directly, checking whether Tint's SPIR-V reader correctly turns a surviving `OpSpecConstant` + `SpecId` decoration into a WGSL `@id(N) override` on its own.
3. **Verify the existing `WGPUConstantEntry`/`use_override_path` plumbing actually works** once fed real overridable WGSL — there's no evidence anything in the current shader corpus has ever produced `has_override_declarations == true`, so this path may have latent bugs of its own (untested for years is not the same as working). Write a small standalone test shader with a real spec constant surviving step 2's change, precompile it once, and confirm the pipeline-creation code correctly passes its value via `WGPUConstantEntry` and produces correct output at two different specialization values without any second Tint conversion.
4. **Re-run the full regression suite plus a targeted spec-constant-heavy scene** (SDFGI is the known heaviest user of the legacy path per the Round 20-23 trail) after the change, and specifically re-attempt the abandoned RW-storage-texture-split fix in `_create_module_with_spec_constants()` to see if it's now moot (no longer reachable) rather than something that still needs its own separate fix.
5. **For whatever legacy-path usage remains** (any spec constant step 1/2 determine genuinely can't be made overridable): only *then* extend `wgsl_precompile.py`'s (and, once item 5 lands, the export-time baker's) ahead-of-time coverage for those specific cases. Values may be scene/asset-dependent (e.g. light or decal counts), so this likely means identifying a bounded, enumerable subset (e.g. boolean feature toggles with only 2 states) rather than attempting full combinatorial coverage, and documenting explicitly which specialization patterns are structurally excluded from ahead-of-time coverage and why.
6. **Verification for the whole item**: `shader_corpus` and `preprocessing_tests` must stay green throughout (they don't exercise specialization constants directly today — a new fixture may be needed to actually cover this path, since its near-total lack of dedicated test coverage is part of why `has_override_declarations`'s permanent falseness went unnoticed this long); `driver_unit_tests` fully green; native editor + full Emscripten web rebuilds; and a live browser re-test of at least one real specialization-constant-heavy feature (SDFGI is the natural candidate) confirming correct rendering, not just successful pipeline creation.

---

## Not tracked here: Task 11 (extension-support abort on load)

Task 11 (`webgpu_notes/TASKS.md`) has its root cause conclusively identified — a 4-byte heap-buffer-overflow inside the pinned `emdawnwebgpu` port's `WGPUInstanceImpl` constructor, i.e. a genuine bug in the pinned Emscripten toolchain, not this fork's own code. It isn't actionable from within this repo without vendoring a toolchain patch or upgrading Emscripten, so it's intentionally left out of the ordered list above; revisit it alongside any future `emsdk-upgrade`-style toolchain bump rather than as standalone work here.

---

## Suggested order of attack

**Rewritten 2026-10-07.** The original ordering routed everything through item 5 (landing the
export-time shader-baking work), because that work was uncommitted when this plan was written. It
has since landed, which dissolved most of the dependency graph: items 5, 6, 7 and 11 are done, 1 and
2 were done in September, and 10 is substantially done with one named next step. **Only items 3, 8
and 9 remain from this plan, and none of them depends on another** — plus item 4's benchmark
follow-on, and a set of newer items this plan never tracked (below).

Read `webgpu_notes/HANDOFF.md` §8 alongside this. It carries the newer work in priority order and is
kept current; this file is the September plan with its outcomes recorded.

### What is left from this plan

| # | Item | Depends on | Effort |
|---|------|-----------|--------|
| 3 | CI: `wgsl_precompile.py` byte-reproducibility double-run diff | nothing (item 5's second `tint_convert_cli --batch` call site has landed, so this now covers more than it would have in September) | ~1 day |
| 8 | Task 10.3 — re-triage stale TODOs 7.8, 7.15, 7.17, 7.18 | nothing. **7.17 is the one to look at first**: item 11 landed, and removing the legacy per-value-combination shader-module path may have made its leak moot | half a day |
| 9 | Task 10.1 / 10.2 / 10.4 — post-compatibility sweep | nothing. **10.2 first** — static analysis, no GPU needed, and the 4.8 merge is exactly the event it exists to catch | ~2-3 days |
| 4′ | Task 9.16's follow-on: the 7-scene benchmark pass + README numbers table | nothing; always a follow-on, never part of the Linux port | hours + run time |

### Newer items this plan never tracked

These came out of work done after September and live in `webgpu_notes/TASKS.md` /
`HANDOFF.md` §8 rather than here. Listed so this file does not read as the whole picture:

- **Decide whether threading is worth having**, now that `threads=yes dlink_enabled=yes` boots
  (Task 14 subtask 1.5.8.5, untouched). Two questions in order: does it stay up beyond startup,
  and does it buy anything? Measurement already says threads *cannot* move shader/pipeline work
  off the critical path.
- **Attribute Task 14's remaining stall term** with a Chrome trace (item 10's next step).
- **Profile a dlink export under network throttling** (Task 46 §4) — `index.side.wasm` is 51 MB
  fetched non-streaming; 96 ms on localhost but ~20 s at 20 Mbit/s. Cheapest high-value
  measurement left, never taken.
- **A real-GPU profile of a Forward+ baked export** — the configuration this fork ships, and the
  one no number in Task 46 covers (§7a was measured on swiftshader, which runs Forward Mobile).
- **Audit the storage-format class** (HANDOFF §4.4) rather than finding the next instance by
  running scenes.
- **Delete or annotate the dead `_depth_alias` code** (HANDOFF §4.5), with the caveat recorded
  there.
- **Report `0002` upstream** (`misc/emsdk_patches/`) so the patch can retire itself.
- **Task 5.2**: Android/iOS browser matrix still `wip`; desktop is done.
- **Task 39**: texture compression as an export option — desktop settled, Safari/mobile unmeasured.
- **Task 11** (extension-support abort) remains out of scope here — see the section above.
- **Braced GDScript** (`webgpu_notes/Braced GDScript (.gdb) — Implementation Plan.md`) is a
  293-line plan that is untracked in git, referenced nowhere in `TASKS.md`, has no implementation
  in the tree, and targets the superseded 4.7.2 sync. It needs either a task entry or a note in
  the file saying it is parked.

---

## Phase-Based Action Plan — superseded

The phase tables that stood here grouped all 11 items by dependency, and every phase after the
first depended on **Phase 2, "land the export-time shader-baking work"**. That work is committed
(`drivers/webgpu/spirv_to_wgsl.{h,cpp}`, `wgsl_bake_subprocess.{h,cpp}`,
`editor/shader/shader_baker/shader_baker_export_plugin_platform_webgpu.{h,cpp}`), its crash tail was
chased through Tasks 31-34 to `{baked: 360, translated: 0}` on real hardware, and Task 25 removed
the specialization-constant half of it outright. With its root gone the graph described
dependencies that no longer exist, and kept listing finished work as pending:

| Old phase | Then | Now |
|---|---|---|
| 1 — Quick wins | done | done (benchmark-numbers pass still outstanding, as item 4′) |
| 2 — Land shader baking | the blocker for 3-6 | **done** (items 5, 11) |
| 3 — CI determinism check | after Phase 2 | **still open** — this plan's item 3 |
| 4 — Remaining Tint failures | needs Phase 2 | **done** (item 6, closed 2026-10-07 against a measured sweep) |
| 5 — Post-compatibility sweep | needs 1, 2, 3 | **still open** — this plan's items 8 and 9, and it never really needed Phase 3 |
| 6 — Loading / progress bar | needs Phase 2 | **substantially done** (item 10), one named next step |
| 7 — Spec-constant fallback | independent | **done** (item 11, Task 25) |

Rather than maintain a second ordering that drifts from `TASKS.md` and `HANDOFF.md`, the remaining
work is listed flat in "What is left from this plan" above. Nothing in it blocks anything else in
it, so a phase graph buys nothing now. `HANDOFF.md` §8 is the priority-ordered list to work from.
