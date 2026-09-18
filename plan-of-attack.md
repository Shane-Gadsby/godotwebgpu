# Resolution Plan — Remaining Work Only (Updated 2026-09-19)

Based on a full re-read of `webgpu_notes/TASKS.md` (through Task 15), `drivers/webgpu/README.md`, `CLAUDE.md`, `.github/workflows/webgpu_tests.yml`, and the current working-tree diff. **Everything already verified `DONE` has been removed** — for the full history of completed work (round-by-round investigation trails, exact fixes, commit hashes), see `webgpu_notes/TASKS.md` and `git log`. This doc only tracks what's still open, ordered **easiest to hardest**.

**Repo state**: on `webgpu-4.7.2`. The SDFGI brightness-runaway bug (the original public-build blocker) is fixed and live-verified. Task 12's `threads=yes` crash for the common case (`dlink_enabled=no`) is fixed and live-verified (commit `6893725f61`); the GDExtension+threads combination (`dlink_enabled=yes threads=yes`) is root-caused to an Emscripten-internal dylink/pthread initialization race and deliberately left unpatched (see item 8 below). Task 15's Tint ICE regression from the emsdk upgrade is fixed. There is no hard blocker on this project; everything below is incremental hardening.

**Uncommitted work in the tree right now**: a substantial, largely-working implementation of export-time shader baking (Task 13) — new `drivers/webgpu/spirv_to_wgsl.{h,cpp}`, `drivers/webgpu/wgsl_bake_subprocess.{h,cpp}`, `editor/shader/shader_baker/shader_baker_export_plugin_platform_webgpu.{h,cpp}`, plus changes to `drivers/webgpu/rendering_shader_container_webgpu.*`, `drivers/webgpu/spirv_preprocess.*`, `drivers/webgpu/tint_cli/main.cpp`, `editor/editor_node.cpp`, three platforms' `detect.py`, and `drivers/SCsub`/`drivers/webgpu/SCsub`. It's been live-verified against the user's real `cameraSim` project (real crashes found and fixed along the way — see item 6) but is **not yet committed**, and its own notes record a currently-broken clean `platform=web target=template_release` build (see item 6). Item 6 below covers finishing and landing this.

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

## 5. Commit the export-time shader-baking work already done, then close its two known open tails

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

## 6. Task 8.3 — remaining pre-existing (non-regression) Tint conversion failures

**Effort: 1-2 days.** Six shaders were originally identified as failing Tint conversion independent of the 4.7.2 upstream sync. Status per shader, re-checked against `webgpu_notes/TASKS.md`:
- `tonemap.glsl:bicubic{,_1d_lut}:frag` — already fixed as a side effect of Task 8.2's `inline_opaque_functions` pass. No action needed.
- `screen_space_reflection_filter.glsl:default:comp` — root-caused and fixed (missing `layout(rgba16f, ...)` qualifier on the `dest` storage image, matching its sibling shader) but **not yet live-verified against a real build** per the write-up's own note. Do that verification (rebuild, rerun `tint_convert_cli`/`wgsl_precompile.py`, confirm this shader now converts) and close it out.
- `tonemap_mobile.glsl:subpass:frag` / `tonemap_mobile.glsl:subpass_1d_lut:frag` — likely unreachable on WebGPU at all (subpass/input-attachment variants, and WebGPU has no subpass support per this driver's own documented design). Confirm unreachability first; if confirmed, add both to `expected_failures.json` rather than attempting a fix.
- `volumetric_fog.glsl:default:comp` — a Tint crash with the same *signature* as Task 8.2's `ConvertUserCall` bug class but a different trigger (no `area_light_atlas`/`ltc_evaluate` references). Needs the same instrumentation approach used to root-cause Task 8.2 before assuming the same fix applies.
- `voxel_gi_debug.glsl:default:vert` — a `read_write` storage buffer used in the vertex stage, which WGSL disallows but GLSL/Vulkan permits. Needs the buffer split into a read-only vertex-stage view.
- `sdfgi_debug_probes.glsl:default:vert` — the vertex entry point's `position` builtin output isn't surviving the SPIR-V round-trip; needs tracing through the preprocessing passes to find where it's lost.

**Verification**: `shader_corpus` and `preprocessing_tests` stay green; `wgsl_precompile.py`'s real-engine-shader sweep drops its Tint-failure count for each shader fixed, with any confirmed-unreachable variant moved to `expected_failures.json` instead.

---

## 7. Document Task 12's remaining `threads=yes dlink_enabled=yes` limitation

**Effort: minutes to an hour — documentation only, the investigation is already done.** Task 12's common-case `threads=yes` crash is fixed and live-verified (commit `6893725f61`). The GDExtension+threads combination (`dlink_enabled=yes threads=yes`) is root-caused precisely to an Emscripten-internal dylink+pthread initialization race in `libdylink.js`'s `postInstantiation()`/`addEmAsm()` — genuinely outside this fork's own code, and deliberately left unpatched rather than forking Emscripten's runtime glue. This decision just needs to be written down where it's discoverable:
1. Update `CLAUDE.md`'s build-commands section and `drivers/webgpu/README.md`'s limitations list (see item 1) to state that `threads=yes dlink_enabled=no` is fully supported, while `threads=yes dlink_enabled=yes` is a known-unsupported combination pending an upstream Emscripten fix (not something this fork intends to patch).
2. Consider filing the issue upstream against Emscripten (searchable prior art likely exists for `MAIN_MODULE`+`PTHREADS` dlopen-ordering races) — optional, not required to close this item.

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

## 10. Task 14 — reduce post-download loading blocking, make the progress bar representative

**Effort: 1 day, needs a real GPU + browser session.** `platform/web/js/engine/preloader.js`'s `animateProgress()` computes progress purely from bytes downloaded, with no visibility into WASM compile/instantiate time, the async `GPUDevice` request cycle, precompiled-WGSL table setup, or (until item 5 above closes it) any runtime Tint shader-fallback conversions — any of which can produce a visible stall after the bar already reads 100%.

**Depends on item 5** (Task 13) being far enough along that a runtime shader-fallback stall isn't misattributed to something else here — an unclosed export-time-baking gap is itself a source of post-100% blocking this task would otherwise chase in the wrong place.

1. Instrument and quantify each post-download phase (WASM fetch-complete → instantiate-complete, device-request start → resolve, engine `main()` start → first frame) against both a small export and the user's real `cameraSim`-scale project, to find the actual largest contributor rather than guessing.
2. Reduce real blocking time where possible: confirm `WebAssembly.instantiateStreaming` is actually taken (not silently falling back due to MIME-type/header issues), and confirm the JS shell's device pre-initialization is kicked off in parallel with the WASM fetch, not serialized after it.
3. Extend `preloader.js`'s progress model to reserve estimated weight for the post-download phases, wired to real phase-completion signals (WASM instantiated, device acquired, first frame rendered) via `Config.prototype.onProgress`, rather than a fixed-timer guess — and handle a phase taking far longer than expected with a graceful "initializing renderer..." style indicator instead of a bar that freezes or snaps to 100% then hangs.
4. Verify with before/after timing numbers against the user's real project, and visually confirm behavior under both a throttled network (stresses download-progress) and a cold cache (stresses post-download phases).

---

## 11. Eliminate the runtime Tint-conversion fallback for specialization-constant variants

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

Items 1-4 are quick, independent, and can be done in any order or in parallel — none blocks another. Item 5 (committing and finishing the already-substantial export-time shader-baking work) is next: it's mostly landing work already done rather than new design, and item 10 explicitly depends on it being far enough along. Items 6-9 (the remaining Tint failures, the Task 12 doc closure, and the two re-triage/audit sweeps) are all independent of each other and of item 5, and can be interleaved with it. Item 10 should wait until item 5 is reasonably settled. Item 11 is the only large piece of remaining work; it's fully independent of everything else here and can be picked up whenever there's a multi-day block of time available, starting with its own step 1 (confirming `freeze_spec_constant_ops`'s constraints) before assuming the override-path fix is safe.

---

## Phase-Based Action Plan

Same 11 items, regrouped into phases by **dependency, not by item number** — a phase can only start once every phase above it that it depends on is done. Within a phase, rows are unordered/parallelizable unless a sub-item note says otherwise. Effort is cumulative per phase in the summary row.

### Phase 1 — Zero-dependency quick wins

Nothing here depends on anything else in this plan; all four can run fully in parallel.

| # | Item | Sub-items | Effort | Status |
|---|------|-----------|--------|--------|
| 1.1 | Write down Task 12's `dlink_enabled=yes threads=yes` decision (plan item 7) | Investigation already done — just record it in `webgpu_notes/TASKS.md` (already recorded) and feed the one-line summary into 1.2 | minutes | ✅ done (folded into 1.2) |
| 1.2 | Fix stale `drivers/webgpu/README.md` limitations (plan item 1) | Remove "no subgroup operations" and "mobile renderer auto-selected" claims; add the `threads=yes` support-matrix line from 1.1 | minutes | ✅ done, committed `902f5c3059` |
| 1.3 | CI: commit `screenshot-comparison` baselines (plan item 2) | Run once to generate; commit under `webgpu_tests/screenshot_comparison/`; drop `--update-baselines` from the workflow | hours | ✅ done, committed `902f5c3059` |
| 1.4 | Port `run_benchmark.sh` to Linux (plan item 4) | Parameterize binary path + browser launch; swap BSD `sed -i ''` for portable equivalents; verify WebGL + WebGPU/Mobile legs; treat a Forward+ leg as a follow-on | 1-2 days | ✅ port done, both known-safe legs verified end-to-end, committed `300ae98cd5`; only the full 7-scene README-numbers pass (out of this item's original scope) remains |

**Phase 1 total**: ~1-2 days. All four items done. The full 7-scene benchmark-numbers pass and README table update (always item 4's own follow-on, not part of the port) can be picked up separately whenever a full benchmark run is worth the time.

---

### Phase 2 — Land the export-time shader-baking work (groundwork for Phases 4 and 6)

Depends on: nothing (the code already exists uncommitted) — but everything in Phases 4 and 6 depends on this phase landing.

| # | Item | Sub-items | Effort |
|---|------|-----------|--------|
| 2.1 | Review and commit the uncommitted Task 13 diff (plan item 5, step 1) | Confirm the diff matches `TASKS.md`'s completion notes; commit as-is, no scope creep | hours |
| 2.2 | Fix the broken clean `platform=web target=template_release` build (plan item 5, step 2) | **Must happen before 2.3-2.4 can be verified.** Rebuild one platform at a time (never concurrent `scons` runs); root-cause the `register_module_types.gen.cpp`/per-module `SCsub` mismatch | hours-1 day |
| 2.3 | Chase the remaining Tint crash tail (plan item 5, steps 3-5) | Use `WEBGPU_BAKE_DEBUG_DUMP` + `cuda-gdb` on the 14 unidentified crashes + the new `atomics.cc:413` Phony case; fix via new `spirv_preprocess` passes or vendored Tint patches; re-run `shader_corpus`/`preprocessing_tests` after each fix; re-export and re-verify in-browser each round | open-ended, budget 1-2 days |
| 2.4 | Update docs once the tail plateaus (plan item 5, step 6) | Flip Task 13's status in `TASKS.md`; update `CLAUDE.md`'s shader-pipeline diagram (now a three-tier lookup) and `drivers/webgpu/README.md` | hours |

**Phase 2 total**: ~1-2 days. **Sequence within phase: 2.1 → 2.2 → (2.3 in parallel with re-verification) → 2.4.**

---

### Phase 3 — CI determinism check (benefits from Phase 2, otherwise independent)

Depends on: nothing strictly, but Phase 2 adds a second `tint_convert_cli --batch` call site (the export-time baker) with its own subprocess/threading concerns — checking determinism after that lands catches more than checking it before.

| # | Item | Sub-items | Effort |
|---|------|-----------|--------|
| 3.1 | Add a `wgsl_precompile.py` double-run diff to CI (plan item 3) | Run twice back-to-back, diff `wgsl_precompiled.gen.h`, fail on any difference | ~1 day |

**Phase 3 total**: ~1 day. Can technically run before Phase 2, but re-run once more after Phase 2 lands to cover the new subprocess path.

---

### Phase 4 — Remaining Tint conversion failures (needs Phase 2's stable baking/testing setup)

Depends on: Phase 2 (verifying these against a known-clean build and baking pipeline, rather than the currently-broken one).

| # | Item | Sub-items | Effort |
|---|------|-----------|--------|
| 4.1 | Verify the already-fixed `screen_space_reflection_filter.glsl` | Rebuild, rerun `tint_convert_cli`/`wgsl_precompile.py`, confirm conversion now succeeds, close out | hours |
| 4.2 | Confirm/dismiss the two `tonemap_mobile.glsl` subpass variants | Check reachability on WebGPU (no subpass support); if unreachable, add to `expected_failures.json` | hours |
| 4.3 | Root-cause `volumetric_fog.glsl:default:comp` | Same crash signature as Task 8.2's `ConvertUserCall` bug, different trigger — needs the same instrumentation approach | half a day |
| 4.4 | Fix `voxel_gi_debug.glsl:default:vert` | Split the `read_write` vertex-stage storage buffer into a read-only view | half a day |
| 4.5 | Fix `sdfgi_debug_probes.glsl:default:vert` | Trace where the `position` builtin output is lost through the preprocessing passes | half a day |

**Phase 4 total**: 1-2 days, sub-items independent of each other.

---

### Phase 5 — Post-compatibility sweep (needs Phases 1, 2, and 3's outputs to trust its own results)

Depends on: Phase 1.3 (screenshot baselines must be real before 5.1 can trust that tier), Phase 2 (a clean, current rebuild to sweep against).

| # | Item | Sub-items | Effort |
|---|------|-----------|--------|
| 5.1 | Full local CI run — Task 10.1 | Rebuild native editor + full web template; confirm zero Tint failures; run full `local_ci.sh` (not `--quick`); triage genuine regressions vs. stale drift; file new bugs as their own tasks | hours (mostly build time) |
| 5.2 | Base-class interface audit — Task 10.2 | Diff the three RDD/RCD/shader-container headers against the pre-Phase-8-sync commit; confirm every changed method has a real WebGPU override; re-check `API_TRAIT_*` (now includes Task 12's new trait) against base-class defaults; record audit outcome | 1 day |
| 5.3 | Re-triage stale TODOs 7.8, 7.15, 7.17, 7.18 — Task 10.3 | Re-confirm each still applies post-sync; **7.17 specifically should be re-checked after Phase 6 (item 11) lands**, since fixing the spec-constant legacy path may make its leak moot; update each `Status` in `TASKS.md` | half a day |
| 5.4 | Fresh live testing against the real project — Task 10.4 | Capture native-Vulkan + WebGPU baselines for scenes/features not yet covered by Task 9.5 or Phase 2's export testing; diff for drift (favor long captures); file confirmed issues as new tasks | 1-2 days |

**Phase 5 total**: ~2-3 days. 5.1/5.2/5.4 can run in parallel; 5.3's item on Task 7.17 should be revisited once Phase 6 lands rather than closed early.

---

### Phase 6 — Loading blocking / progress bar (needs Phase 2)

Depends on: Phase 2 — an unclosed export-time-baking gap would otherwise be misattributed as a loading-time problem here.

| # | Item | Sub-items | Effort |
|---|------|-----------|--------|
| 6.1 | Instrument and quantify post-download phases | WASM instantiate, device request, first-frame timing, against small and real-project-scale exports | hours |
| 6.2 | Reduce actual blocking time | Confirm `instantiateStreaming` is taken; confirm device pre-init runs in parallel with WASM fetch, not serialized after it | hours |
| 6.3 | Make the progress bar representative | Reserve tail-percentage weight for post-download phases in `preloader.js`; wire real phase-completion signals via `Config.prototype.onProgress`; add a graceful stalled-but-not-frozen indicator | half a day |
| 6.4 | Verify | Before/after timing on the real project; visual check under network throttle and cold cache | hours |

**Phase 6 total**: ~1 day.

---

### Phase 7 — Eliminate the spec-constant runtime Tint fallback (largest; independent, but touches 5.3's Task 7.17)

Depends on: nothing structurally — can start any time — but is the natural last phase given its size, and its outcome should feed back into Phase 5.3's Task 7.17 re-triage.

| # | Item | Sub-items | Effort |
|---|------|-----------|--------|
| 7.1 | Confirm why `freeze_spec_constant_ops` evaluates everything unconditionally | Check git history/original rationale; check Tint SPIR-V reader gaps for `OpSpecConstantOp` computed expressions | 1 day |
| 7.2 | Prototype the minimal override-preserving version | Skip evaluation only for spec constants consumed as plain runtime values; hand-construct a test SPIR-V module and confirm via `tint_convert_cli` | 1-2 days |
| 7.3 | Verify the existing `WGPUConstantEntry`/`use_override_path` plumbing | Feed it real overridable WGSL for the first time ever; confirm correct output at two specialization values with no second Tint conversion | 1 day |
| 7.4 | Full regression + targeted spec-constant-heavy scene (SDFGI) | Re-run full suite; re-attempt the abandoned RW-storage-texture-split fix to see if it's now moot | 1 day |
| 7.5 | Close out remaining legacy-path usage | Extend `wgsl_precompile.py`/the Phase 2 export-time baker's ahead-of-time coverage for whatever can't be made overridable; document exclusions explicitly | open-ended |
| 7.6 | Final verification | `shader_corpus`/`preprocessing_tests` green (new fixture likely needed); `driver_unit_tests` green; native + full web rebuild; live browser re-test of a real spec-constant-heavy feature | 1 day |

**Phase 7 total**: multiple days, open-ended at 7.5. Start with 7.1 before assuming the rest is safe.

---

### Phase summary

| Phase | Depends on | Cumulative effort |
|-------|-----------|--------------------|
| 1 — Quick wins | none | ~1-2 days |
| 2 — Land shader baking | none | ~1-2 days |
| 3 — CI determinism check | Phase 2 (best done after) | ~1 day |
| 4 — Remaining Tint failures | Phase 2 | 1-2 days |
| 5 — Post-compatibility sweep | Phases 1, 2, 3 | ~2-3 days |
| 6 — Loading/progress bar | Phase 2 | ~1 day |
| 7 — Spec-constant fallback elimination | none structurally; feeds back into 5.3 | multiple days, open-ended |
