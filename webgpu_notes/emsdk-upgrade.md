# emsdk / Dawn / Tint Upgrade Plan

**Status**: `IN PROGRESS` — Phase 0.1 done (branch `emsdk-upgrade` created off `webgpu-4.7.2`, tag `pre-emsdk-upgrade` on prior `HEAD`). Phase 1.1/1.2 done (target-version table pinned, breaking-change audit against the real Emscripten ChangeLog complete). Phase 1.3 (new-feature audit against the actual vendored `webgpu.h`) blocked on 2.2's local install — cannot be confirmed from source alone. Phase 0.2 (known-good test baseline capture) not yet started.

**Correction (2026-09-18)**: the `5e16f308c7` commit message claimed the `pre-emsdk-upgrade` tag was created as part of Phase 0.1, but it was never actually pushed to the repo — `git tag -l` showed nothing. Created for real this session, pointing at `2e128619e3` (the actual `emsdk-upgrade`/`webgpu-4.7.2` merge-base, i.e. true prior `HEAD`).

**Phase 0.1.3 baseline record** (as of `pre-emsdk-upgrade`):
- `EM_VERSION`: `4.0.11` (`.github/workflows/web_builds.yml:12`, `.github/workflows/webgpu_tests.yml:23`)
- `EMSDK_VERSION` default: `4.0.11` (`build-linux.sh:53`)
- `EmsdkVersion` default: `4.0.11` (`build-windows.ps1:55`)
- Tint commit: `db49a5496374b1f7284e0b9c8f2964c01d4bb20a` (2026) (`thirdparty/README.md` `## tint`)
- spirv-tools commit: `3605cce5b11f6a085107fd400f1721cd2a59c49e` (2026) (`thirdparty/README.md` `## spirv-tools`)
- spirv-headers: `spirv.h`/`spirv.hpp` at vulkan-sdk-1.4.335.0 (`b824a462d4256d720bebb40e78b9eb8f78bbb305`, 2025); `spirv.hpp11` at `ad9184e76a66b1001c29db9b0a3e87f646c64de0` (2026) (`thirdparty/README.md` `## spirv-headers`)
**Goal**: move this fork's toolchain from the currently-pinned **Emscripten 4.0.11** to the latest stable emsdk (≈**6.0.x** as of 2026-09), bring the vendored `thirdparty/tint` (and its `spirv-tools`/`spirv-headers` extraction) forward to match, replace the driver workarounds that exist *only* because 4.0.11's `webgpu.h`/Dawn were too old, and re-verify Forward+ end-to-end so the fork lands back at (at minimum) parity with where `webgpu_notes/TASKS.md` Task 9.5 currently stands.

**Why now**: `TASKS.md` already documents four driver limitations explicitly blamed on the pinned toolchain version (texture-view `usage` override, 16-bit norm formats, multi-draw-indirect, subgroup limits reporting 0) — see "Gains" below. One of them (texture-view `usage`) is the exact missing piece identified for the SDFGI root-cause investigation (Task 9.5, item 7's original blocker, worked around via `RD::SUPPORTS_SHAREABLE_TEXTURE_FORMATS` in Round 21). This plan treats the upgrade as infrastructure work that should land *before* further SDFGI Round 45+ investigation, since a newer Dawn may also change or fix the hazard-tracking behavior Round 39-44 are actively chasing.

**Ground rule for the whole plan**: this is a toolchain swap, not a rewrite. Every phase below is built to fail loud and cheap (native build → shader corpus → driver unit tests → full local CI → real-GPU live pipeline, in that order) so a regression is caught at the cheapest tier, not after a 20-minute web template rebuild.

---

## Phase 0: Baseline & Safety Net

Do this before touching any pinned version. The entire point is to have a trustworthy "before" snapshot to diff every later phase against — Task 9.5 already learned the hard way (Round 23) that silent stale-build false negatives are the single biggest risk in this kind of change.

### 0.1 Branch and tag the current state
- 0.1.1 Create a working branch off `webgpu-4.7.2` (e.g. `emsdk-upgrade`).
- 0.1.2 Tag current `HEAD` (e.g. `pre-emsdk-upgrade`) so `git diff`/`git bisect` against the exact pre-upgrade state stays trivial later.
- 0.1.3 Record the current pinned versions in one place for later diffing: `EM_VERSION` (`.github/workflows/web_builds.yml`, `.github/workflows/webgpu_tests.yml`), `EMSDK_VERSION`/`EmsdkVersion` defaults (`build-linux.sh`, `build-windows.ps1`), and the Tint commit (`thirdparty/README.md`'s `## tint` section — currently `db49a5496374b1f7284e0b9c8f2964c01d4bb20a`).

### 0.2 Capture a full "known good" test baseline
- 0.2.1 Run `./webgpu_tests/local_ci.sh` (full rebuild + full suite) on current `HEAD` and save the output/report.
- 0.2.2 Run the Task 9.5 real-GPU live pipeline (`WEBGPU_REAL_GPU=1 webgpu_tests/scene_smoketest/run_scenes.mjs`) and `webgpu_tests/screenshot_comparison` against the user's real scratch project (per `LIVE_REPRO_METHODOLOGY.md`), covering every item `TASKS.md`'s "Confirmed working right now" line currently lists: base Forward+ 3D, SSR, SSAO, SSIL, Glow, Fog, Volumetric Fog, Adjustments/BCS, and SDFGI (SDFGI renders but still has the open brightness-runaway bug, item 11 — capture that as a *known* baseline defect, not a new regression, so Phase 6 doesn't chase it as new).
- 0.2.3 Save screenshots/console logs from 0.2.2 next to the local_ci report as the literal diff target for Phase 6.
- 0.2.4 Note current `bin/tint_convert_cli` behavior on `webgpu_tests/shader_corpus/expected_failures.json` — the accepted-failure baseline must be re-diffed after the Tint bump, not silently inherited.

### 0.3 Confirm scope boundaries
- 0.3.1 Decide, and write down here, whether this upgrade also re-syncs `wasm-feature-detect`/other minor Emscripten-adjacent thirdparty bits, or is scoped strictly to emsdk + Dawn/Tint + spirv-tools/spirv-headers. (Default recommendation: scope strictly to the four named toolchain pieces; anything else is a separate task.)

---

## Phase 1: Toolchain Version Survey & Compatibility Audit

Research only — no code changes. The goal is a concrete target-version table and a pre-flight list of every breaking change that could plausibly touch this repo, so Phase 2 isn't discovery-driven.

### 1.1 Pin exact target versions — `DONE` (2026-09-18)
- 1.1.1 Latest stable emsdk release tag: **`6.0.9`** (confirmed via `emscripten-core/emsdk` and `emscripten-core/emscripten` GitHub tag lists — both show `6.0.9` as the newest tag; the docs site's "6.0.10-git (dev)" label is the in-progress unreleased head, not a tagged release).
- 1.1.2 `emdawnwebgpu` port version pinned by `tools/ports/emdawnwebgpu.py` **at the `6.0.9` tag itself** (not `main`, which can move ahead of a given release): `_VERSION = 'v20260423.175430'`. That tag resolves (verified via the GitHub API, not the docs summary — see correction below) to Dawn commit **`b975919dfb45406ca17162e4d44c74a650caa679`**.
  - **Verification note**: `WebFetch`'s page summary of the `google/dawn` release page initially returned a second, different-looking hash (`31e25af254ab572c77054edec4946d2244e184dd`) alongside the correct one, attributed to "dawn.googlesource.com" — that second hash does not resolve to any real ref on `dawn.googlesource.com` (confirmed 404 via direct Gerrit/Gitiles API query) and appears fabricated by the fetch summarizer. Treat single-source `WebFetch` summaries of version/commit hashes as unverified until cross-checked against a structured API (used `api.github.com/repos/google/dawn/git/refs/tags/...` here, which returned the hash directly, unsummarized).
- 1.1.3 Dawn `src/tint/` commit at that point: **same as 1.1.2** — unlike `spirv-tools`/`spirv-headers`, Tint isn't a separate DEPS-pinned repo; it lives at `src/tint/` inside the Dawn tree itself, so "the Tint commit" *is* the Dawn commit (`thirdparty/README.md`'s existing `## tint` entry already records it this way — `db49a5496374b1f7284e0b9c8f2964c01d4bb20a` is a Dawn commit hash, not a separate Tint-repo hash).
- 1.1.4 `spirv-tools`/`spirv-headers` revisions pinned by Dawn's `DEPS` at `b975919dfb45406ca17162e4d44c74a650caa679` (fetched directly from `raw.githubusercontent.com/google/dawn/<sha>/DEPS`):
  - `third_party/spirv-tools/src`: `ff5c50339cc1e9f34f04cb440a3e5fe89db0161d` (changed from current `3605cce5b11f6a085107fd400f1721cd2a59c49e` — needs a real re-sync in Phase 3.3)
  - `third_party/spirv-headers/src`: `ad9184e76a66b1001c29db9b0a3e87f646c64de0` — **identical** to what `thirdparty/README.md` already has pinned for `spirv.hpp11` (re-synced ahead of schedule per that entry's own note, "2026"). No change needed for the hpp11 half; the `spirv.h`/`spirv.hpp` half is still pinned separately to a Vulkan-SDK tag (`b824a462d4256d720bebb40e78b9eb8f78bbb305`) per that entry's existing split-pin note — re-check in 3.3.2 whether Dawn's DEPS pin has caught up enough to collapse the split, but don't assume it has.

### Final version table (target)

| Component | Current (`pre-emsdk-upgrade`) | Target |
|---|---|---|
| emsdk / `EM_VERSION` | `4.0.11` | `6.0.9` |
| Dawn / Tint (`src/tint/`) commit | `db49a5496374b1f7284e0b9c8f2964c01d4bb20a` | `b975919dfb45406ca17162e4d44c74a650caa679` |
| emdawnwebgpu port version | (pre-port-pinning, N/A) | `v20260423.175430` |
| spirv-tools | `3605cce5b11f6a085107fd400f1721cd2a59c49e` | `ff5c50339cc1e9f34f04cb440a3e5fe89db0161d` |
| spirv-headers (`spirv.hpp11`) | `ad9184e76a66b1001c29db9b0a3e87f646c64de0` | unchanged (already current) |
| spirv-headers (`spirv.h`/`spirv.hpp`) | vulkan-sdk-1.4.335.0 (`b824a462d4256d720bebb40e78b9eb8f78bbb305`) | re-check in 3.3.2, not yet re-surveyed |

### 1.2 Breaking-change audit against this repo specifically — survey `DONE` (2026-09-18), code audit still pending (that's Phase 2's job)
Go through the Emscripten `ChangeLog.md` entries between 4.0.11 and the target version and flag anything that intersects this repo's actual build flags. Read the real changelog (`raw.githubusercontent.com/emscripten-core/emscripten/6.0.9/ChangeLog.md`) rather than inferring from release notes summaries — confirmed every candidate below actually landed, with exact version and PR number:
- 1.2.1 **`FAKE_DYLIBS` disabled by default — confirmed at `6.0.0`** (#25930): "`-shared` will produce real dynamic libraries by default (`-sSIDE_MODULE` is implied)... if you include real dynamic libraries in your link command emscripten will now automatically produce a dynamically linked program (`-sMAIN_MODULE=2` is implied)." This repo's web build uses `dlink_enabled=yes` (`CLAUDE.md`'s documented build command) — **still needs the actual code audit** of `platform/web/detect.py`/`platform/web/SCsub` in Phase 2.4, this entry only confirms the change is real and exactly where it landed.
- 1.2.2 **Windows tool launchers become `.exe` instead of `.bat`/`.ps1` — confirmed at `6.0.0`** (#24858): old `.bat` files recoverable via `tools/maint/create_entry_points.py --bat-files` if needed as a stopgap. `build-windows.ps1` audit still pending (Phase 2.1.2/2.4).
- 1.2.3 **`DEFAULT_TO_CXX` disabled by default — confirmed at `6.0.6`** (#11121), exactly the version the plan guessed: "`em++` is now required when linking C++ programs... old behavior is still available using `-sDEFAULT_TO_CXX`." SCons audit still pending.
- 1.2.4 **Minimum browser version bumps — confirmed at `6.0.0`** (#26677): `MIN_CHROME_VERSION` 74→85, `MIN_FIREFOX_VERSION` 68→79, `MIN_SAFARI_VERSION` 12.2→14.1, with a **further** Safari bump to `15.0` at `6.0.9` itself (#27542, "removes legacy JS polyfills and Binaryen lowering passes"). Still expected irrelevant per the plan's original reasoning (WebGPU already requires newer browsers than this floor) — `platform/web/js/` audit still pending.
- 1.2.5 Nothing else materially version-gate-relevant found between 4.0.11 and 6.0.9 in a `-sUSE_PTHREADS`/`-sSTACK_SIZE`/closure-compiler direction beyond routine version bumps (closure compiler → `20260429.0.0` at 6.0.0, libcxx/libcxxabi → LLVM 22.1.8 at 6.0.6) — none of these are breaking in a way `detect.py`'s existing gates wouldn't already tolerate.
- 1.2.6 `-sUSE_WEBGPU=1` removal (5.0) is **already handled** — `platform/web/detect.py:269-283` already uses `--use-port=emdawnwebgpu` unconditionally once `cc_semver >= (4, 0, 10)`. Nothing to do here beyond confirming the port still resolves under the new emsdk's port registry (mechanical, Phase 2.4).

### 1.3 New WebGPU/Dawn feature audit (the actual "gains" list) — `BLOCKED on 2.2` (local emsdk install), not yet started
For each item below, confirm presence in the *target* version's vendored `webgpu.h` (via `~/emsdk/upstream/emscripten/cache/sysroot/include/webgpu/webgpu.h` after installing/activating the new version) before assuming it's available — don't infer from Chrome release notes alone, since emdawnwebgpu's own API surface lags browser Dawn slightly.
- 1.3.1 **Texture view `usage` override** (`WGPUTextureViewDescriptor.usage`, landed in Dawn/Chrome ~132) — grep the new `webgpu.h` for a `usage` field on `WGPUTextureViewDescriptor`. This is the field `TASKS.md` (Task 9.5, item 7 root-cause writeup) confirmed does *not* exist in the 4.0.11-vendored header.
- 1.3.2 **16-bit norm texture formats** (`WGPUTextureFormat_R16Unorm`/`Snorm`, `RG16Unorm`/`Snorm`, `RGBA16Unorm`/`Snorm`, feature `unorm16-texture-formats`/`snorm16-texture-formats`) — grep `WGPUFeatureName` and `WGPUTextureFormat` enums. `TASKS.md`'s "16-bit norm formats" entry confirmed these are entirely absent from the 4.0.11 header, not just unrequested.
- 1.3.3 **Multi-draw-indirect** — check for a `wgpuRenderPassEncoderMultiDrawIndirect`-equivalent entry point or a `multi-draw-indirect` feature name.
- 1.3.4 **Subgroup operations** — check `WGPUFeatureName` for `subgroups`/`subgroups-f16` and whether `WGPUSupportedLimits`/`WGPUAdapterInfo` now exposes real subgroup min/max size fields (currently this driver reports `LIMIT_SUBGROUP_IN_SHADERS` as `0` unconditionally).
- 1.3.5 Record a yes/no + exact symbol names for each of 1.3.1-1.3.4 in a short table before starting Phase 5 — Phase 5's scope depends entirely on which of these actually landed in emdawnwebgpu specifically (not just in browser Dawn).

---

## Phase 2: emsdk Bump (Mechanical)

Pure version-pin work. No driver logic changes in this phase — the goal is "same behavior, newer toolchain," so any compile/link break here is purely a toolchain-compat fix, never a feature adoption (that's Phase 5).

### 2.1 Update pinned versions
- 2.1.1 `build-linux.sh`: bump the `EMSDK_VERSION` default.
- 2.1.2 `build-windows.ps1`: bump the `EmsdkVersion` default.
- 2.1.3 `.github/workflows/web_builds.yml`: bump `EM_VERSION`.
- 2.1.4 `.github/workflows/webgpu_tests.yml`: bump `EM_VERSION`.
- 2.1.5 Bump the version-check floor in `platform/web/detect.py` (`cc_semver < (4, 0, 10)` at line ~273) only if 1.2 turned up a *new* minimum requirement for a feature this driver now depends on (Phase 5) — otherwise leave the floor as-is; a build with an older-but-still-supported emsdk should keep working, it just won't get the Phase 5 features.
- 2.1.6 Update version references in docs: `CLAUDE.md`'s "Emscripten 4.0.10+" line, `drivers/webgpu/README.md`'s "Emscripten 5.x with emdawnwebgpu port" prerequisite line, `README.md` if it names a version.

### 2.2 Install and activate locally
- 2.2.1 `./emsdk install <version> && ./emsdk activate <version>` (or the Windows equivalent) against a scratch `emsdk` checkout — don't clobber a working install until the new one is confirmed functional.
- 2.2.2 `source emsdk_env.sh` (or `.ps1`) and confirm `emcc --version` reports the target version.

### 2.3 Native builds first (fastest signal, no Emscripten involved)
- 2.3.1 `./drivers/webgpu/tint_cli/build.sh --clean` — confirms `bin/tint_convert_cli` still builds. This doesn't touch emsdk at all, but re-running it now establishes a clean starting point before Phase 3 touches vendored Tint.
- 2.3.2 `scons platform=linuxbsd target=editor dev_build=yes -j$(nproc)` (or macOS equivalent) — confirms shared engine code is unaffected. Should be a no-op given emsdk changes don't touch native builds; run it anyway as a cheap sanity check before spending time on the web build.

### 2.4 Web template build
- 2.4.1 `scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j$(nproc)` under the newly-activated emsdk.
- 2.4.2 Work through any compile/link errors. Expected sources, in likely order of appearance: SCons toolchain detection changes (`platform/web/detect.py`, `platform/web/emscripten_helpers.py`), removed/renamed Emscripten linker flags, `--use-port=emdawnwebgpu` resolving to a different (possibly incompatible) port API shape.
- 2.4.3 If the `emdawnwebgpu` port's C API shape changed (field renames, struct layout changes) between 4.0.11 and target, fix call sites in `drivers/webgpu/rendering_context_driver_webgpu.cpp`/`rendering_device_driver_webgpu.cpp` as pure mechanical renames — do **not** use this pass to also adopt new features (Phase 5's job).
- 2.4.4 Once it links, do a minimal smoke boot (`webgpu_tests/local_ci.sh --quick`) before moving on.

### 2.5 CI dry run
- 2.5.1 Push the branch and let `.github/workflows/web_builds.yml` / `webgpu_tests.yml` run against the new `EM_VERSION` in a clean CI environment (catches local-environment-masked issues, e.g. a stale cached emsdk install locally hiding a real activation failure).

---

## Phase 3: Vendored Library Sync (Tint / spirv-tools / spirv-headers)

Tint is vendored independently of emsdk (extracted from a pinned Dawn commit via `extract_tint.sh`, per `thirdparty/README.md`), so this is a separate, decoupled bump — sequence it after Phase 2 succeeds, so a Tint-caused regression is never confused with an emsdk-caused one.

### 3.1 Re-extract Tint
- 3.1.1 Run `extract_tint.sh` (or whatever the current script path is — confirm via `thirdparty/README.md`'s "extracted via" note) against the Tint commit identified in 1.1.3.
- 3.1.2 Diff the newly-extracted `thirdparty/tint/src/tint/` tree against the current vendored copy to get a sense of change volume before applying patches (a small diff vs. a near-total rewrite changes how much manual patch-conflict work to expect).

### 3.2 Re-apply the 9 vendored patches
Reapply each patch from `thirdparty/README.md`'s `## tint` section in order, resolving conflicts individually rather than force-applying:
- 3.2.1 `0001-skip-block-layout-validation.patch` (`SetSkipBlockLayout(true)` for Godot UBO layout)
- 3.2.2 `0002-allow-struct-member-size-mismatch.patch` (`kAllowStructMemberSizeMismatch` for spec constants)
- 3.2.3 `0003-decompose-strided-array-stride-guard.patch` (skip padding when stride < element size)
- 3.2.4 `0004-accept-non-constant-point-size.patch` (accept non-constant `point_size` stores)
- 3.2.5 `0005-size-emission-and-capability.patch` (`@size` emission guard for spec constants)
- 3.2.6 `0006-remove-abseil-dependency.patch` (`absl::from_chars` → `std::from_chars`)
- 3.2.7 `0007-support-spirv-1-4-target-env.patch` (SPIR-V validation target env bump)
- 3.2.8 `0008-handle-phony-texture-usages.patch` (`Phony` instruction handling)
- 3.2.9 `0009-allow-phony-instructions-capability.patch` (`kAllowPhonyInstructions` capability)
- 3.2.10 For each patch that no longer applies cleanly: check whether upstream Tint already fixed/subsumed the underlying issue (in which case drop the patch and note it in `thirdparty/README.md`) before manually resolving the conflict — several of these were workarounds for specific Tint bugs that may since be fixed upstream.

### 3.3 Re-sync spirv-tools / spirv-headers
- 3.3.1 Re-pin `thirdparty/spirv-tools` and `thirdparty/spirv-headers` to the revisions identified in 1.1.4 (Dawn's `DEPS` at the target Tint commit).
- 3.3.2 Re-verify `thirdparty/spirv-headers`'s broader-than-upstream-Godot extraction set (per its `thirdparty/README.md` entry, extracted specifically for Tint's SPIR-V reader) still covers everything the new Tint's reader needs — a newer Tint may reference newer SPIR-V enum/extension entries.

### 3.4 Rebuild and test at the cheapest tier
- 3.4.1 `./drivers/webgpu/tint_cli/build.sh --clean`.
- 3.4.2 `cd webgpu_tests/shader_corpus && ./compile_fixtures.sh && node run_tests.mjs` — this is the fastest, most direct signal on whether the new Tint changed SPIR-V→WGSL behavior in a way that breaks this repo's shaders, and doesn't require any engine build.
- 3.4.3 Diff the new run's failures against `webgpu_tests/shader_corpus/expected_failures.json` (the Vulkan-only-variant baseline from Phase 0.2.4). Any *new* failure not in that baseline needs triage: either a new preprocessing pass is needed (per `CLAUDE.md`'s architecture note — "the fix is almost always either a new/extended preprocessing pass here or a Tint patch, not a WGSL-writer change"), or the shader is a genuinely new Vulkan-only variant that belongs in the baseline.
- 3.4.4 `cd webgpu_tests/preprocessing_tests && node run_tests.mjs` — confirms the 12 (now-possibly-13, see Task 8.2) C++ SPIR-V passes still produce input the new Tint accepts.
- 3.4.5 Update `thirdparty/README.md`'s `## tint` version line (new commit hash) and `## spirv-tools`/`## spirv-headers` entries once this phase is green.

---

## Phase 4: Update `wgsl_precompile.py` / Build-Time Pipeline Sanity

Not a feature phase — just confirming the build-time precompiler survives the Phase 2/3 bumps before Phase 5 starts changing driver logic on top of it.

### 4.1
- 4.1.1 Run a full `scons ... webgpu=yes` build (Phase 2.4) end to end and confirm `wgsl_precompiled.gen.h` is generated without new Tint ICE aborts (per `CLAUDE.md`'s debugging note: run `tint_convert_cli <file.spv>` directly, un-forked, on anything that fails in batch mode to see the real crash diagnostic).
- 4.1.2 If new ICEs appear, use `WGSL_DEBUG_DUMP=<substring>` / `TINT_DEBUG_DUMP_PREPROCESSED=<path>` (both already exist for exactly this purpose) to isolate the failing shader before deciding whether it's a preprocessing-pass gap or an upstream Tint regression worth reporting.

---

## Phase 5: Adopt Native Toolchain Features (Replace Workarounds)

This is the "update the work we've done so far to use native emsdk changes" phase. Gate every item on Phase 1.3's actual findings — only touch driver code for features confirmed present in the *target* emdawnwebgpu's `webgpu.h`, and treat each as independently optional/revertable (don't block the whole phase on one item).

### 5.1 Texture view `usage` override → revisit the SDFGI shareable-format workaround
- 5.1.1 **Only if 1.3.1 confirmed the field exists.** Re-read Task 9.5 Round 21's fix (the `RD::SUPPORTS_SHAREABLE_TEXTURE_FORMATS` driver trait, gating `gi.cpp`'s `lightprobe_data`/`cascade.light_data`/`occlusion_data` between native format-reinterpretation on Vulkan/Metal/D3D12 and a decoded-storage fallback on WebGPU) before changing anything — this was hard-won across many rounds and is currently *working*, just via a different mechanism than the other backends.
- 5.1.2 Prototype native `shareable_formats` support behind the new `usage`-override field: create the narrower-usage view (dropping `StorageBinding` for the read-only reinterpreted view) that Round 21's investigation identified as the missing piece, and test whether `RGB9E5Ufloat`/`R4G4B4A4_UNORM_PACK16`-style reinterpretation now validates.
- 5.1.3 **Decision point, do not skip**: if native shareable-formats now works, weigh reverting `RD::SUPPORTS_SHAREABLE_TEXTURE_FORMATS` to `true` on WebGPU (simpler, matches other backends, removes a WebGPU-specific `gi.cpp` code path) against keeping the Round-21 workaround (battle-tested, zero risk of reintroducing the original black-screen bug class). Recommendation: only revert if the native path passes the full Task 9.5 SDFGI live-capture regression check (Phase 6.4) at least as cleanly as the current workaround — this is a "nice to have, verify hard" change, not a required one.
- 5.1.4 If reverted: remove the now-dead decoded-storage fallback paths in `gi.cpp` and the `SUPPORTS_SHAREABLE_TEXTURE_FORMATS` trait's WebGPU branch; update `rendering_device_driver.cpp`'s `api_trait_get()` base-class default per `CLAUDE.md`'s reminder that any trait removal needs the base-class default audited too.

### 5.2 16-bit norm texture formats
- 5.2.1 **Only if 1.3.2 confirmed the formats/feature exist.** Add native `WGPUTextureFormat_R16Unorm`/`Snorm`, `RG16Unorm`/`Snorm`, `RGBA16Unorm`/`Snorm` entries to `pixel_formats_webgpu.h`'s mapping table, gated behind the `unorm16-texture-formats`/`snorm16-texture-formats` device features (request the feature at device-creation time in `rendering_context_driver_webgpu.cpp`, matching however `timestamp-query` is already optionally requested).
- 5.2.2 Keep the existing Float-equivalent fallback path for devices/browsers that don't support the feature — this must stay a runtime capability check, not a compile-time one, since not every user's browser/GPU will have it even once the toolchain supports requesting it.
- 5.2.3 Update the code comment in `pixel_formats_webgpu.h` (and the `TASKS.md` entry it corresponds to) that currently says "no upgrade path currently exists."
- 5.2.4 Add/extend a driver unit test (`webgpu_tests/driver_unit_tests/test_format_mapping.mjs`) covering both the native-format and fallback branches.

### 5.3 Multi-draw-indirect
- 5.3.1 **Only if 1.3.3 confirmed an entry point/feature exists.** Locate the current per-draw indirect loop in `rendering_device_driver_webgpu.cpp` (`CLAUDE.md`'s "Format constraints" note: "no multi-draw-indirect (loop over individual draws)").
- 5.3.2 Add a capability-gated fast path using the native multi-draw call when available, keeping the existing loop as the fallback for devices/browsers without the feature.
- 5.3.3 Benchmark before/after on a scene with a meaningfully high per-frame indirect-draw count (check `webgpu_tests`'s benchmark scenes / `benchmark_vs_threejs.md` methodology) — this is a perf change, not a correctness one, so it needs a number attached, not just "compiles."

### 5.4 Subgroup operations
- 5.4.1 **Only if 1.3.4 confirmed real limits reporting.** Update `LIMIT_SUBGROUP_IN_SHADERS` (currently hardcoded to `0`) in `rendering_device_driver_webgpu.cpp` to query and report the real device-reported subgroup size when the `subgroups` feature is present, `0` otherwise.
- 5.4.2 Audit whether any Forward+ shader path in `servers/rendering/renderer_rd/` is currently branching on this limit being `0` to take a non-subgroup fallback path — flipping it to a real nonzero value could route those shaders down an untested subgroup-enabled path for the first time on this backend. Treat this as a real behavior change requiring its own regression pass, not a transparent capability bump.
- 5.4.3 Given 5.4.2's risk, recommend landing this *disabled* (still hardcoded `0`) in the same PR as the rest of Phase 5, with real-value reporting as a deliberately separate, later follow-up once it's been tested in isolation.

### 5.5 Re-run the shader corpus + driver unit tests after each 5.x sub-item
- 5.5.1 Don't batch all of 5.1-5.4 before testing — land and verify each independently, since a regression caused by 5.1's SDFGI change should never be diagnosed alongside a regression from 5.3's indirect-draw change.

---

## Phase 6: Regression Sweep — Getting Back to a Fully Working Forward+

This is the "whatever changes will be required to get back to a fully working forward+ implementation" phase. A Dawn/Tint bump can silently change WGSL codegen shape or Dawn's own validation/hazard-tracking behavior even where nothing in this repo's own code changed — so every previously-fixed, toolchain-sensitive bug in `TASKS.md` needs a live re-check, not just a trust-the-old-fix assumption.

### 6.1 Full standalone test suite
- 6.1.1 `cd webgpu_tests/shader_corpus && ./compile_fixtures.sh && node run_tests.mjs`
- 6.1.2 `cd webgpu_tests/driver_unit_tests && node run_tests.mjs`
- 6.1.3 `cd webgpu_tests/preprocessing_tests && node run_tests.mjs`
- 6.1.4 `cd webgpu_tests/resource_lifecycle && node run_tests.mjs`
- 6.1.5 `cd webgpu_tests/screenshot_comparison && node run_tests.mjs`
- 6.1.6 `./webgpu_tests/local_ci.sh` (full rebuild + full suite, not `--quick`) — diff its report against the Phase 0.2.1 baseline.

### 6.2 Re-verify every Tint/Dawn-version-sensitive fix already on record
Specifically re-test these, since they're the ones most likely to silently break or silently "un-break" from a codegen/validation change (not an exhaustive re-read of all of `TASKS.md` — these are the ones with a documented dependency on Tint/Dawn's exact behavior):
- 6.2.1 **Task 8.2** — the `ConvertUserCall` texture-parameter-propagation Tint bug (13th SPIR-V preprocessing pass workaround, triggered by 4.7's LTC area-light feature). Check whether the new Tint fixed this upstream; if so, decide whether to keep the workaround pass (harmless if redundant) or remove it (cleaner, but re-verify nothing else depends on its side effects).
- 6.2.2 **Task 9.5 Round 23** — the fragment-stage spec-constant read/write-storage-split + stage-visibility fix (`_create_module_with_spec_constants()`). Re-verify with the new Tint's WGSL output shape.
- 6.2.3 **Task 9.5 Round 36** — the `flatten_binding_arrays` cascade-array-collapse limitation (Tint flattens dynamically-indexed texture arrays to a single resource, silently making every cascade read cascade 0). Confirm whether the new Tint version still has this limitation before assuming it's unchanged — this is a real architectural gap independent of the emsdk bump, but worth a two-minute check now that Tint has moved.
- 6.2.4 **Task 9.5, item 11 (open, in-progress)** — the SDFGI brightness-runaway hazard-tracking race (Rounds 33-44, currently hypothesized as a Dawn cross-submission write-visibility gap on `lightprobe_average_tex`'s same-texel cross-frame read-modify-write). **Explicitly re-run the existing repro (`webgpu_tests/sdfgi_race_repro/`) against the new Dawn version before continuing Round 45's planned parallelism experiment** — if newer Dawn changed or fixed its hazard-tracking behavior, this could resolve on its own, which would make Round 45's planned work moot. Do this re-check early in Phase 6, since it changes the priority of ongoing work outside this upgrade too.
- 6.2.5 **Task 9.5, item 9 (low priority, open)** — canvas preferred-format mismatch. Re-check `navigator.gpu.getPreferredCanvasFormat()` behavior; unrelated to the toolchain bump but cheap to re-verify while already in this code.

### 6.3 Real-GPU live pipeline — base coverage
- 6.3.1 Re-run `WEBGPU_REAL_GPU=1 webgpu_tests/scene_smoketest/run_scenes.mjs` against the 8 in-repo `benchmark_*` scenes.
- 6.3.2 Re-run against the user's real scratch project (per `LIVE_REPRO_METHODOLOGY.md`, never the user's actual project files directly — rsync into a scratch dir as Task 9.5's established workflow does).
- 6.3.3 **Verify the export-template freshness gotcha from Task 9.5 Round 23 doesn't repeat**: confirm the scratch project's `export_presets.cfg` `variant/extensions_support` setting routes through the freshly-built `dlink_enabled` template, and `md5sum` the exported `index.side.wasm` against the just-built `bin/` wasm before trusting any "no change"/"still broken" result.

### 6.4 Real-GPU live pipeline — full feature matrix
Re-confirm every item currently on `TASKS.md`'s "Confirmed working right now" line, one at a time or in the same combinations Task 9.5 originally used:
- 6.4.1 Base Forward+ 3D rendering
- 6.4.2 SSR
- 6.4.3 SSAO
- 6.4.4 SSIL
- 6.4.5 Glow
- 6.4.6 Fog
- 6.4.7 Volumetric Fog
- 6.4.8 Adjustments/BCS
- 6.4.9 SDFGI (expect the item-11 runaway still present unless 6.2.4 found it resolved by the Dawn bump; expect the item-7/Round-21 workaround's behavior unchanged unless Phase 5.1 was adopted and verified)
- 6.4.10 If Phase 5.1-5.4 landed any changes, specifically stress-test the combinations those items touch (e.g. 5.1's SDFGI reinterpretation change gets extra scrutiny in 6.4.9; 5.3's multi-draw path needs a scene with many indirect draws in the mix).

### 6.5 Native (non-web) regression check
- 6.5.1 Since `drivers/webgpu/` never compiles into non-web builds, confirm nothing in Phase 3-5's shared-engine-code touches (if any, e.g. 5.1's `gi.cpp` changes) regressed native Vulkan/Metal rendering — rebuild the native editor and spot-check the same scenes on Vulkan.

### 6.6 Sign-off criteria
- 6.6.1 `local_ci.sh` fully green, diffed clean against the Phase 0 baseline (any new `expected_failures.json` entries justified and documented).
- 6.6.2 6.4's full feature matrix matches Phase 0.2.2's baseline exactly, with any *intentional* deltas (from Phase 5 adoption or a Dawn-side fix found in 6.2.4) explicitly called out and justified, not just observed.
- 6.6.3 No new console errors/warnings in a 60s+ live capture (per Task 9.5 Round 20's methodological correction: short captures miss async Dawn validation errors — always capture 60s+ before trusting a "clean" result).

---

## Phase 7: Documentation & Cleanup

### 7.1 Update version references (finalize what Phase 2.1.6 started)
- 7.1.1 `CLAUDE.md` — Emscripten version requirement, `emsdk_env.sh` example.
- 7.1.2 `drivers/webgpu/README.md` — prerequisite line, and its "Known Limitations" section: remove/qualify any limitation Phase 5 actually resolved (16-bit norm formats, multi-draw-indirect, subgroups — only the ones genuinely adopted and verified, not just toolchain-available).
- 7.1.3 `thirdparty/README.md` — confirm Phase 3.4.5's version bumps are all in (tint, spirv-tools, spirv-headers).
- 7.1.4 `webgpu_site/TECHNICAL_REFERENCE.md` / `CORRECTNESS_AND_COMPATIBILITY.md` — update anything describing the current toolchain version or the limitations Phase 5 changed.

### 7.2 Update `webgpu_notes/TASKS.md`
- 7.2.1 Add a new dated entry (following the existing per-task Status/Severity/Lines/Issue/Investigation format) documenting: the version bump itself, which Phase 5 items were adopted vs. deferred and why, and the outcome of Phase 6's regression sweep — especially 6.2.4's SDFGI-runaway re-check result, since that directly affects Task 9.5's next-round priority.
- 7.2.2 If 6.2.3 found the cascade-array-flattening limitation resolved or changed, update that item's status.
- 7.2.3 If 5.1 was adopted, update Task 9.5 item 7's writeup to describe the new native mechanism rather than leaving only the Round-21 workaround history — keep the Round-21 history for context (it explains *why* the workaround existed), but mark it superseded.

### 7.3 This document
- 7.3.1 Once all phases are complete and Phase 6.6 sign-off is met, update this document's Status line to `DONE` and add a short closing summary (final version table, what was adopted from Phase 5, what was deferred and why) at the top, mirroring `TASKS.md`'s "CURRENT STATUS" convention.

---

## Phase 8: Rollout

### 8.1
- 8.1.1 Open a PR from the upgrade branch into `webgpu-4.7.2` including the full Phase 6 verification evidence (test output, live-capture screenshots) in the description — this is a toolchain-wide change touching every shader path, so the review bar should be "show the regression sweep," not just "CI is green."
- 8.1.2 After merge, confirm the CI workflows (`.github/workflows/web_builds.yml`, `.github/workflows/webgpu_tests.yml`) are genuinely running the new `EM_VERSION` on the target branch, not a cached runner image.
- 8.1.3 Note the completed upgrade in any relevant `sync/*` branch tracking notes (`webgpu_notes/TASKS.md` Phase 8) if those branches will need the same bump applied separately.
