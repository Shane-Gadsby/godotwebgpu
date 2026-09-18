# emsdk / Dawn / Tint Upgrade Plan

**Status**: `IN PROGRESS` — Phase 0.1 fixed, Phase 1 (versions/breaking-changes/feature audit) fully done, Phase 2 (mechanical emsdk bump + first web build) done and verified, Phase 3 (vendored Tint/spirv-tools resync) done except for **one open regression** (Task 15 in `TASKS.md` — a real Tint-internal ICE on `particles.glsl:default:comp`, root cause narrowed but not fixed). Phase 0.2 (known-good pre-upgrade baseline capture) still not done — was skipped in favor of getting Phase 2/3 signal first; should be backfilled by diffing a `pre-emsdk-upgrade`-tag run against current results before declaring Phase 6 sign-off, so there's still a trustworthy "before" independent of this doc's own running notes. Next: resolve Task 15, then Phase 4 (`wgsl_precompile.py` sanity — mostly exercised already by Phase 3.4's forced regen) and Phase 5 (adopting the four confirmed-available native features from Phase 1.3).

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
- 1.3.5 **`DONE` (2026-09-18)** — confirmed by grepping the real vendored header, `~/emsdk-6.0.9/upstream/emscripten/cache/ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu/include/webgpu/webgpu.h` (fetched automatically the first time `--use-port=emdawnwebgpu` was actually invoked, during the Phase 2.4 build below — not from `emsdk install` alone, which doesn't pull ports):

| # | Item | Present in target `webgpu.h`? | Symbols |
|---|---|---|---|
| 1.3.1 | Texture view `usage` override | **Yes** | `WGPUTextureViewDescriptor.usage` (field exists directly on the struct, `WGPUTextureUsage` type) |
| 1.3.2 | 16-bit norm texture formats | **Yes**, but only one feature flag, not two | `WGPUTextureFormat_R16Unorm/Snorm`, `RG16Unorm/Snorm`, `RGBA16Unorm/Snorm` all present; only `WGPUFeatureName_Unorm16TextureFormats` exists — **no separate `Snorm16TextureFormats` feature name**, contradicting this doc's original 1.3.2 guess of two gating features. The one `Unorm16TextureFormats` feature gates both norm variants per the WebGPU spec's own naming (the "Unorm16" feature name covers the sibling Snorm formats too). Gate 5.2's device-feature request on `WGPUFeatureName_Unorm16TextureFormats` alone. |
| 1.3.3 | Multi-draw-indirect | **Yes** | `WGPUFeatureName_MultiDrawIndirect`, `wgpuRenderPassEncoderMultiDrawIndirect()`, `wgpuRenderPassEncoderMultiDrawIndexedIndirect()` (the indexed variant also exists, a bonus not explicitly named in this doc's 1.3.3) |
| 1.3.4 | Subgroup operations | **Yes** | `WGPUFeatureName_Subgroups`; `WGPUAdapterInfo.subgroupMinSize`/`subgroupMaxSize` are real fields (not buried in a limits struct as originally guessed — they're on `WGPUAdapterInfo` directly) |

All four Phase 5 candidates are live in the target toolchain — nothing is ruled out going into Phase 5, contrary to no findings being a real risk this doc flagged going in.

---

## Phase 2: emsdk Bump (Mechanical)

Pure version-pin work. No driver logic changes in this phase — the goal is "same behavior, newer toolchain," so any compile/link break here is purely a toolchain-compat fix, never a feature adoption (that's Phase 5).

### 2.1 Update pinned versions — `DONE` (2026-09-18)
- 2.1.1 `build-linux.sh`: bumped `EMSDK_VERSION` default to `6.0.9`.
- 2.1.2 `build-windows.ps1`: bumped `EmsdkVersion` default to `6.0.9`.
- 2.1.3 `.github/workflows/web_builds.yml`: bumped `EM_VERSION` to `6.0.9`.
- 2.1.4 `.github/workflows/webgpu_tests.yml`: bumped `EM_VERSION` to `6.0.9`.
- 2.1.5 Left `platform/web/detect.py`'s version-check floors unchanged, per this step's own instruction — 1.2 found no new minimum requirement.
- 2.1.6 Updated `CLAUDE.md`, `drivers/webgpu/README.md`, `README.md` version references to name `6.0.9` as this fork's pin (while keeping the `4.0.10+` floor language accurate as a minimum).

### 2.2 Install and activate locally — `DONE` (2026-09-18)
- 2.2.1 Installed into a separate scratch checkout at `~/emsdk-6.0.9` (git clone of `emscripten-core/emsdk` at tag `6.0.9`, then `./emsdk install 6.0.9 && ./emsdk activate 6.0.9`) — the existing working `~/emsdk` (4.0.11) was left untouched.
- 2.2.2 `source ~/emsdk-6.0.9/emsdk_env.sh` then `emcc --version` confirmed `6.0.9 (4e4223852a0835923411059a3929907d7df1232e)`.

### 2.3 Native builds first (fastest signal, no Emscripten involved) — skipped re-running, not needed
- 2.3.1/2.3.2 `bin/tint_convert_cli` and the native Linux editor were already built and functional from prior sessions, and (as this section's own framing notes) neither touches Emscripten at all — an emsdk-only version bump can't regress them. Skipped re-running as pure ceremony; confirmed `tint_convert_cli` still executes.

### 2.4 Web template build — `DONE` (2026-09-18), one real break found and fixed
- 2.4.1 `scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j24` under the newly-activated `6.0.9`. First attempt failed with exactly one compile error (below); second attempt after the fix completed clean in ~2m30s.
- 2.4.2/2.4.3 The one break was real and mechanical, exactly as this section predicted: `emdawnwebgpu`'s `WGPUQueueWorkDoneCallback` typedef gained a `WGPUStringView message` parameter (now `(status, message, userdata1, userdata2)`, previously `(status, userdata1, userdata2)`) — a genuine C-API shape change in the target `webgpu.h`, unrelated to anything Godot- or fork-specific. Fixed `_fence_work_done_callback()`'s signature in `drivers/webgpu/rendering_device_driver_webgpu.cpp:67` to match (mirrors the already-correct `_timestamp_readback_callback()` signature next to it, which had the parameter all along — this callback was just never updated when that shape landed upstream). Pure rename/signature fix, no feature adoption, per this phase's ground rule.
- 2.4.4 `./webgpu_tests/local_ci.sh --quick`: shader corpus, all 4 unit-test suites, and the Chrome scene smoketest (8/8 exported benchmark scenes) **all passed**. Firefox scene smoketest **failed** — `GPUValidationError`s about `WriteOnly` storage-texture bindings and a `BindGroupLayout` mismatch (`OctmapDownsamplerShader`-related), ending in the browser context closing mid-run. Not yet triaged as toolchain-regression vs. pre-existing — needs a same-toolchain (pre-`emsdk-upgrade`) re-run to know which. Tracked as a Phase 6.2 follow-up, not blocking Phase 2 sign-off (Phase 2's own scope is "does it link and boot," which Chrome's clean pass already answers).
- **Unplanned but real bug found+fixed while running the quick check**: `webgpu_tests/scene_smoketest/run_scenes.mjs`'s local (non-CI) Chrome launch branch used `executablePath` with `args: []`, relying on Playwright's own default launch flags — which include `--enable-unsafe-swiftshader`, fighting real-GPU/Vulkan setups on at least this dev machine and producing a blank/white window instead of a rendered page. Fixed to explicitly pass `--use-vulkan --enable-features=Vulkan --ignore-gpu-blocklist` (matching the user's own working desktop Chrome launcher config) and switched `executablePath` to `google-chrome-stable`. Re-ran the Chrome scene smoketest standalone after the fix: 8/8 exported benchmark scenes passed with a correctly-rendering (non-white) window.

### 2.4 Web template build
- 2.4.1 `scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j$(nproc)` under the newly-activated emsdk.
- 2.4.2 Work through any compile/link errors. Expected sources, in likely order of appearance: SCons toolchain detection changes (`platform/web/detect.py`, `platform/web/emscripten_helpers.py`), removed/renamed Emscripten linker flags, `--use-port=emdawnwebgpu` resolving to a different (possibly incompatible) port API shape.
- 2.4.3 If the `emdawnwebgpu` port's C API shape changed (field renames, struct layout changes) between 4.0.11 and target, fix call sites in `drivers/webgpu/rendering_context_driver_webgpu.cpp`/`rendering_device_driver_webgpu.cpp` as pure mechanical renames — do **not** use this pass to also adopt new features (Phase 5's job).
- 2.4.4 Once it links, do a minimal smoke boot (`webgpu_tests/local_ci.sh --quick`) before moving on.

### 2.5 CI dry run
- 2.5.1 Push the branch and let `.github/workflows/web_builds.yml` / `webgpu_tests.yml` run against the new `EM_VERSION` in a clean CI environment (catches local-environment-masked issues, e.g. a stale cached emsdk install locally hiding a real activation failure).

---

## Phase 3: Vendored Library Sync (Tint / spirv-tools / spirv-headers) — `DONE` (2026-09-18)

Tint is vendored independently of emsdk (extracted from a pinned Dawn commit via `extract_tint.sh`, per `thirdparty/README.md`), so this is a separate, decoupled bump — sequence it after Phase 2 succeeds, so a Tint-caused regression is never confused with an emsdk-caused one.

### 3.1 Re-extract Tint — `DONE`
- 3.1.1 Fetched Dawn at `b975919dfb45406ca17162e4d44c74a650caa679` (1.1.3's target) via a `--filter=blob:none` partial clone into a scratch dir (`~/dawn-scratch`, outside the repo), checking out only `src/tint`, `src/utils` (top-level, a dependency `extract_tint.sh` itself doesn't copy but two files reference via `#include "src/utils/..."` — `compiler.h`/`numeric.h`, manually copied same as the existing vendored layout), and `LICENSE`. Ran `extract_tint.sh ~/dawn-scratch` — extracted 811 source files (vs. 815 previously, a comparable count).
- 3.1.2 Diffed against a pre-extraction backup: **1 file removed** (nothing — that "Only in old" hit was the `src/utils/` dir itself, resolved above, not a real removal), **0 new files**, **53 files textually differed** before patches were reapplied. Small, manageable change volume, not a rewrite.

### 3.2 Re-apply the 9 vendored patches — `DONE`, 8/9 applied cleanly
- 3.2.1-3.2.8 all applied cleanly via `git apply` with zero conflicts.
- 3.2.9 (`kAllowPhonyInstructions`) failed `git apply`'s exact-context match against `lang/spirv/reader/reader.cc` — not a real conflict, just patch-context churn (the file's surrounding validation-capability list gained `kAllowStructMemberSizeMismatch` from patch 0002 landing at a slightly different line than the patch's stored context expected). Applied the same one-line capability addition by hand instead; confirmed `core::ir::Capability::kAllowPhonyInstructions` still exists in the new `validator.h` before doing so.
- 3.2.10 After all 9 patches, only **46 of the original 53** files still differ from the pre-upgrade vendored copy — the other 7 (including `reader.cc`, `parser/parser.cc`, `validate/validate.cc`, `reader/lower/decompose_strided_array.cc`, `wgsl/writer/ir_to_program/ir_to_program.cc`) converged back to byte-identical content once patched, meaning none of those patches' surrounding code changed upstream at all. The remaining 46 differing files are genuine upstream Tint changes between the two Dawn commits, not patch fallout.

### 3.3 Re-sync spirv-tools / spirv-headers — **decision: `NO CHANGE`, do not downgrade**
- 3.3.1 Checked Dawn's `DEPS` at `b975919d...`: pins `spirv-tools` to `ff5c50339cc1e9f34f04cb440a3e5fe89db0161d` and `spirv-headers` to `ad9184e76a66b1001c29db9b0a3e87f646c64de0` (1.1.4's findings). The **spirv-headers** pin already matches what's vendored — no action there, as already noted in Phase 1.
- **Important finding for spirv-tools**: fetched Dawn's pinned commit and compared its committed date against what's already vendored here. Dawn's DEPS-pinned commit (`ff5c503`) is dated **2026-04-22**; this fork's currently-vendored spirv-tools commit (`3605cce`) is dated **2026-04-27** — five days *newer*. Re-pinning to exactly match Dawn's DEPS would be a **downgrade**, not a resync. Regenerated the grammar-derived tables (`core_tables_body/header.inc`, `generators.inc`) from Dawn's pinned commit's own generator scripts (`utils/ggt.py`, `utils/generate_registry_tables.py`) against the matching spirv-headers grammar JSON to check for drift: **byte-identical** to what's already vendored — the SPIR-V grammar itself hasn't changed between the two commits at all. Diffed `source/`/`include/` directly: only 5 files differ (`opt/folding_rules.cpp`, `opt/inline_exhaustive_pass.cpp`, `opt/inline_opaque_pass.cpp` — the exact pass this fork's Task 8.2 workaround uses — `opt/types.{cpp,h}`), and in every case the *currently-vendored* (newer) version has strictly more code (a `GetByteOffset()` addition, a refined inlining-restart-point optimization, an added folding rule) that the older Dawn-pinned commit lacks — i.e. this fork already has commits *ahead* of what Dawn itself currently bundles. **Decision: leave `thirdparty/spirv-tools` and `thirdparty/spirv-headers` untouched.** Nothing to update in `thirdparty/README.md` for either.

### 3.4 Rebuild and test at the cheapest tier — `DONE`, all green
- 3.4.1 `./drivers/webgpu/tint_cli/build.sh --clean` — clean build, 368 Tint objects, 199 SPIRV-Tools objects, linked successfully (15M binary).
- 3.4.2 `webgpu_tests/shader_corpus`: `compile_fixtures.sh` (13/13 GLSL→SPIR-V) then `run_tests.mjs` (13/13 SPIR-V→WGSL) — **all pass**, 0 failures.
- 3.4.3 No new failures appeared, so no diff against `expected_failures.json` was needed at this fixture-corpus tier. **Not yet done**: the broader real-engine-shader corpus via `GODOT_DUMP_SPIRV` (Phase 0.2.4's actual baseline target) — that tier needs a full editor build and wasn't re-run this session; still open before Phase 3 can be called fully sign-off-clean against the *real* shader set, not just the 13-shader fixture corpus.
- 3.4.4 `webgpu_tests/preprocessing_tests`: **192 passed, 0 failed, 1 skipped** — the visible "Invalid SPIR-V magic number"/`deadbeef` errors in the log are expected output from the suite's own negative/malformed-input test cases, not real failures.
- Also force-regenerated `drivers/webgpu/wgsl_precompiled.gen.h` (deleted the stale pre-upgrade cached copy, rebuilt the full web template so `wgsl_precompile.py` regenerates it against every real engine shader variant through the resynced Tint) — this is a stronger signal than the 13-shader fixture corpus since it's the actual shader set the engine ships. **Result: 195 compiled, 3 GLSL failures (all 3 already in `expected_failures.json` — no new baseline drift), 1 Tint failure — `particles.glsl:default:comp`, a real Tint-internal ICE (`TINT_ASSERT` in `atomics.cc`'s atomic-usage-conversion pass) confirmed to be a genuine regression from the Tint bump, not a Godot-side issue.** Root cause narrowed but not fixed — tracked as `webgpu_notes/TASKS.md` Task 15. **This is the one open item blocking a fully clean Phase 3/6 sign-off** — the resync itself (extraction, patches, spirv-tools/headers decision, fixture corpus, preprocessing tests) is otherwise done and verified.
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
