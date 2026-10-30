# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

This is [Godot WebGPU](https://github.com/Shane-Gadsby/godotwebgpu) (started as [dwalter/godotwebgpu](https://github.com/dwalter/godotwebgpu)) — a fork of Godot Engine that adds a WebGPU rendering driver (`drivers/webgpu/`) as a browser export target, implementing the same `RenderingDeviceDriver` / `RenderingContextDriver` / `RenderingShaderContainerFormat` interfaces as the built-in Vulkan, Metal, and D3D12 backends. It targets the **Forward+ (Clustered)** renderer via Emscripten + the `emdawnwebgpu` port (Dawn). Note that `RendererCompositorRD::initialize()` silently falls back to Forward Mobile when the device reports fewer than 48 textures per shader stage, which on WebGPU is the adapter's `maxSampledTexturesPerShaderStage` — so a **software adapter (swiftshader, e.g. a `CI=1` test run) runs Forward Mobile, not Forward+**. `platform/web/js/engine/engine.js` requests that limit at the adapter's own max so real hardware takes the Forward+ branch. This distinction matters whenever a result depends on the scene renderer; see `webgpu_notes/TASKS.md` §1 and §3 trap 1 for the wrong conclusions this has already caused. See `README.md` for the project pitch/demos and `GODOT_README.md` for the original upstream Godot README.

`origin` is `Shane-Gadsby/godotwebgpu` (this fork, where its releases are published); `upstream` is `godotengine/godot`. Base branch for this fork's own work is `webgpu-4.7.2`, now superseded by `webgpu-4.8` (the 4.8 port, `webgpu_notes/TASKS.md` §4.17); `sync/*` branches track merges of newer upstream Godot releases (see §4.3 for the 4.7.2 sync and §4.17 for 4.8).

This fork's history was detached from upstream's by a past rewrite and has since been **repaired** (`webgpu_notes/TASKS.md` §4.18): `webgpu-4.8`, `webgpu-4.7.2` and `main` are grafted back onto upstream's real lineage, so `git merge-base` against `godotengine/godot` is correct and an upstream sync replays the ~2,800 commits it should rather than ~85,000. Nothing special is needed any more — merge upstream normally. Before a sync, run `misc/scripts/upstream_sync_report.py` and follow the playbook in `webgpu_notes/TASKS.md` §3 trap 114. The pre-repair tips are kept on `origin` as `backup/pre-lineage-repair/*`, alongside the `backup/pre-claude-author-strip*` branches that made the repair possible; **do not delete either set.**

## Build Commands

Two build targets matter here: the **native editor** (fast, no Emscripten needed — use this to validate that changes to shared engine code compile, since it exercises everything except the actual WebGPU driver) and the **web WebGPU template** (the real target, requires Emscripten).

```bash
# Native editor — fast iteration, validates shared RenderingDevice/servers code:
scons platform=linuxbsd target=editor dev_build=yes -j$(nproc)   # Linux
scons platform=macos target=editor dev_build=yes -j$(nproc)      # macOS

# Web WebGPU export template — the real target, requires Emscripten 4.0.10+ with emdawnwebgpu (this fork pins 6.0.9):
source ~/emsdk/emsdk_env.sh
scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j$(nproc)
```

**`dlink_enabled=yes` requires a patched Emscripten toolchain**, and `threads=yes` on top of it makes that mandatory: without the patches a threads+GDExtension export does not boot at all (`0001`, an `ASM_CONSTS` initialization race in Emscripten's dylink+pthread glue) and logs ~11 errors per load that silently drop later `Module` exports on worker threads (`0002`, `-sEXPORT_ALL` re-exporting the `HEAP*` views eagerly). Both live in dlink-only code paths and both need a pthread worker to fire, so `threads=no dlink_enabled=yes` is unaffected in practice — but `apply.sh` is all-or-nothing, so just apply it for any dlink build. See `misc/emsdk_patches/` (patches, tooling, and how to add a third) and `webgpu_notes/TASKS.md` §4.5. Apply with `./misc/emsdk_patches/apply.sh --apply`. **`emsdk install` overwrites the toolchain tree, so they must be re-applied after every toolchain change**, not just after an emsdk version bump; a `dlink_enabled=yes` build tells you when they are missing — loudly with `threads=yes`, as a note otherwise. Every build helper (`build.sh`, `build-linux.sh`, `build-macos.sh`, `build-windows.ps1`) and both web CI workflows apply them for you, right after setting up Emscripten. The toolchain's JS glue is hashed into the link's dependencies (`get_toolchain_js_fingerprint()` in `platform/web/detect.py`, consumed in `platform/web/SCsub`), so applying or reverting a patch relinks on its own — no need to delete the target `.js`/`.wasm`/`.zip` by hand. That hash reads `misc/emsdk_patches/emsdk_patch.py`'s own table, so a newly added patch is covered without touching `detect.py`. Before the fingerprint existed, scons did not relink and a stale `bin/godot*.js` silently survived, which is how the `ASM_CONSTS` crash reappeared in the *debug* template hours after the patch had fixed the release one. Threaded builds also need the page served with COOP/COEP cross-origin-isolation headers. A thread-enabled export (`variant/thread_support`) also renders on its own pthread by default — own WebGPU device, canvas as an OffscreenCanvas (`webgpu_notes/TASKS.md` §2.5) — through the `variant/separate_render_thread` export option, which passes `--render-thread separate`; turn it off and the project setting `rendering/driver/threads/thread_model` decides as on other platforms. `threads=no` templates and the Safe model are unaffected.

`webgpu=yes` is a SConstruct option (`SConstruct:201`) that only takes effect combined with `platform=web` — `drivers/webgpu/` isn't compiled into non-web builds at all, so a native build never touches WebGPU driver code.

`bin/wgsl_convert_cli` — the standalone host tool that does build-time SPIR-V→WGSL precompilation (our passes + SPIRV-Cross's WGSL backend) — is a **separate native build**, independent of Emscripten and of the two builds above:

```bash
./drivers/webgpu/wgsl_cli/build.sh           # incremental
./drivers/webgpu/wgsl_cli/build.sh --clean   # full rebuild
```

On Windows, run it from Git Bash with LLVM's `clang++` (auto-detected, or set `CXX`); it produces `bin/wgsl_convert_cli.exe`. Objects go in a per-OS `.build/<os>/` directory, so a checkout shared with WSL doesn't mix ELF and COFF objects. A native editor built with `webgpu=yes` (`WEBGPU_SHADER_BAKER_ENABLED`: export-time WGSL baking) runs `wgsl_convert_cli` from its own executable's directory, so the CI editor artifacts ship it next to the editor (inside `Contents/MacOS` for the macOS `.app`).

Rebuild it directly (rather than through scons) when iterating on `drivers/webgpu/spirv_preprocess.cpp`, `spirv_cross_wrapper.cpp`, or anything under `thirdparty/spirv-cross-wgsl`/`thirdparty/spirv-tools` — it's much faster than a full web build and is what `webgpu_tests/shader_corpus` and `drivers/webgpu/wgsl_precompile.py` (the build-time precompiler, invoked automatically by `scons ... webgpu=yes`) both drive. The precompiled table `drivers/webgpu/wgsl_precompiled.gen.h` is dependency-tracked: `drivers/webgpu/SCsub` fingerprints the vendored SPIRV-Cross/SPIRV-Tools/`wgsl_cli` sources, `bin/wgsl_convert_cli` and glslang's version, so a web build re-bakes on its own after any of them change and skips the bake otherwise (`webgpu_notes/TASKS.md` §4.22).

The **export-time** bake (`ShaderBakerExportPlugin` with the WebGPU platform, `webgpu_notes/TASKS.md` §4.23) runs `wgsl_convert_cli --batch` over all stages at once, in parallel chunks spawned with `posix_spawn` — not one serialized subprocess per stage, which used to be 24 of a 27 s export. Converted WGSL is cached by SPIR-V hash under the tool's own hash at `~/.cache/godot/webgpu_wgsl_bake/` (survives engine rebuilds, invalidated by a converter rebuild; `WEBGPU_BAKE_CACHE=0` / `WEBGPU_BAKE_CACHE_DIR` / `WEBGPU_BAKE_JOBS`), so a warm export is ~4 s against ~8 s cold. Web presets ship the baked cache as `<name>.shaders.pck` (`shader_baker/separate_pack`), which the shell preloads and the engine mounts beside the main pack; **a template built before that change silently loads no shaders at all**, so rebuild the templates with the editor. `shader_baker/variant_pruning` (Automatic by default) leaves out shader groups the target can never select and moves the ones nothing in the project was found to use — Forward+'s advanced group, unless the project uses TAA, FSR 2, LightmapGI, VoxelGI, SDFGI, SSS or a motion-vector compositor effect — into `<name>.shaders_extra.bin`, which the shell fetches as the engine starts and `ShaderRD` reads on a cache miss (`webgpu_notes/TASKS.md` §2.27). With both VRAM compression families exported, `vram_texture_compression/separate_packs` puts each in its own `<name>.textures_*.pck` and the shell fetches only the one the device samples (§2.18). Stale `.godot/exported/<hash>` directories (7 days) and shader-cache group directories from other engine builds are pruned automatically. While a WebGPU bake runs, `RenderingDevice::has_feature()` answers as the target only inside the baker's own callbacks (`RD::ShaderBakeFeatureOverrideScope`, per thread); the editor's own frames in between keep its device's answers (§3 trap 73). A shader whose variants depend on a capability must make them **groups with bake requirements** (fog.cpp, fsr2.cpp) or refresh their text in `ShaderRD::add_general_defines_refresh_callback()` (fsr.cpp), never build a `has_feature()`-dependent variant list once at construction: the baker then bakes the editor's flavor under a cache key the target never asks for.

`misc/scripts/install_accesskit.py` (run by every build helper unless `--skip-install`) downloads AccessKit only when `bin/build_deps/accesskit/version` does not name the version it pins; `--force` reinstalls (§4.32).

`./build-linux.sh` is the full local build: editor, then the **no-threads, no-dlink web pair** (debug + release), installed into `builds/<version>/templates/` at once with a "Ready to export" line (also in the timing table), then the Linux templates and the other six web variants, one SCons run after another in this tree, so every template's baked WGSL matches the editor's baker. `build.sh`, `build-macos.sh` and `build-windows.ps1` build that pair first too, then their `dlink_enabled=yes threads=no` pair (§2.50). `--help` lists the flags: `--skip-install`, `--skip-web`, `--skip-native-templates`, `--skip-wgsl-cli` (`--skip-tint-cli` still accepted), `--force-shader-rebake`, `--linker`, `--jobs`, `--web-targets`/`--web-threads`/`--web-dlink` to narrow the matrix while iterating (a narrowed run cannot guarantee the editor/template match), `--no-ccache`, `--no-web-raycast` (web templates without Embree, so no occlusion culling; it is in them by default, §2.45), `--no-gdscript-braces` (a standard editor and templates, `gdscript_braces=no`; every build in the run gets the same value, so an editor never exports `.gdb` scripts for templates that cannot load them) `--skip-editor-plugins`, and `--plugins-only` (just the plugins: no prerequisites and no SCons, so it is safe while another build runs). `EDITORS_DIR` / `TPZ_COPY_DIR` override the two installs outside the tree (`godot-editors/editors`, `godot-editors/templates`; a missing folder is skipped), so a test run from a worker's worktree does not replace the user's editor. Unless skipped, it ends by building the braced GDScript Rider plugin and VS Code `.vsix` from `../godot-gdb-rider` and `../godot-gdb-vscode` into `builds/<version>/editor_plugins/` (a failure there only warns; Gradle gets a JDK 21 found by the script, not whatever `JAVA_HOME` says). **ccache is on by default** (§4.26): SCons signs every object with the resolved compiler path, so only the first run after that default changed recompiles every variant. `--no-ccache` leaves the ccache symlink directory on `PATH` and sets `CCACHE_DISABLE=1` rather than removing it, so the signed path never changes and opting out — or back in — is free. Use `ccache -C` to discard the cache itself. It prints a per-step timing table at the end. **Never start a `scons` run — not even a no-op read or a timing probe — while another is running in this tree** (`pgrep -ax scons` first): the runs share `.sconsign5.dblite`, so an overlap can lose a run's recorded signatures. The worse half of this is fixed — each variant now writes its own copy of the option-dependent generated sources (`modules/modules_enabled.gen.h` and five others) under `gen/<variant>/`, prepended to `CPPPATH`, so no build can compile against another's copy and look up to date forever (`webgpu_notes/TASKS.md` §4.25, §3 trap 24). A generated source must therefore spell its own includes from the project root, not relative to where it used to live.

## Testing

See `webgpu_tests/README.md` for full detail. In order of speed/cost:

```bash
# Shader corpus — fastest, no engine build needed, just wgsl_convert_cli:
cd webgpu_tests/shader_corpus && ./compile_fixtures.sh && node run_tests.mjs

# Driver unit tests (JS, standalone):
cd webgpu_tests/driver_unit_tests && node run_tests.mjs

# SPIR-V preprocessing pass tests (run_pipeline()'s passes, standalone):
cd webgpu_tests/preprocessing_tests && node run_tests.mjs

# Every engine variant through the converter: interfaces and the driver's WGSL text-scan facts against the stored
# references, WGSL compiled in Chrome (BROWSER=firefox node validate_wgsl.mjs <dirs> asks naga instead). Also in
# local_ci.sh --quick (which accepts any real adapter). Run directly off the 4080, its adapter check refuses unless
# WGSL_VALIDATE_ADAPTER=any (or <vendor>/<arch>); the fallback adapter is never accepted:
webgpu_tests/converter_smoke/run.sh <scratch_dir>
webgpu_tests/wgsl_text_scan/run_tests.sh <smoke run dir>      # the text scans alone; VALIDATE=1 compiles the rewritten text

# Resource lifecycle / screenshot comparison (standalone, need Playwright):
cd webgpu_tests/resource_lifecycle && node run_tests.mjs
cd webgpu_tests/screenshot_comparison && node screenshot_tests.mjs

# Web audio parity: the Sample-playback driver rendered offline against native AudioServer behavior (no build),
# then native vs web end to end (needs the editor + web templates):
cd webgpu_tests/web_audio && node run_tests.mjs && ./export.sh && node run_e2e.mjs

# Every readback form (sync, async, awaitable; textures, buffers, mesh arrays) natively and on the web, plus the
# debug-only "no data yet" warnings (export.sh debug|release first for the web half):
webgpu_tests/async_readback/run_native.sh && cd webgpu_tests/async_readback && node run_web.mjs --browser chrome

# Editor drag-and-drop onto every list (arrays, dictionaries, property arrays, Project Settings lists, bus effects):
# real drags in a windowed editor under Xvfb (headless gives the editor a 2x2 viewport), see tests/editor_drop/README.md:
tests/editor_drop/run.sh

# Editor default folders (editor/default_folders/*): drives the create/save dialogs and their check boxes under Xvfb:
tests/default_folders/run.sh

# Shader-bake isolation: an interactive Web export from a windowed editor (Xvfb, Vulkan) with FSR 2 + TAA in its 3D
# viewport; fails on a "Mismatch uniform type" in the editor's log or an FSR 2 pass baked in the editor's flavor (~1 min):
webgpu_tests/bake_isolation/run.sh

# NaN containment: one NaN pixel through every bokeh DOF mode, glow, SSR and auto exposure, natively on Forward+ and
# Mobile (needs a display or xvfb-run; ONLY=dof,glow,ssr,exposure narrows it); the matrix's *_nan arms are the web side:
webgpu_tests/dof_nan/run.sh

# The project manager's update check against this fork's GitHub releases (an engine unit test: needs an editor
# built with tests=yes, which dev_mode=yes implies):
bin/godot.linuxbsd.editor.dev.x86_64 --test --test-case="*EngineUpdateLabel*"

# WebRTC web/native parity (needs the editor + a web template exported first):
cd webgpu_tests/webrtc_parity && ./export.sh && node run_tests.mjs

# WebRTC under real bandwidth pressure (netem in an unprivileged namespace, no root):
cd webgpu_tests/webrtc_congestion && ./export.sh && ./sweep.sh

# Forward+ feature matrix, 103 arms (needs a real GPU and a fresh ./export.sh); --parity also compares every
# frame against a native Vulkan render of the fixture -- the check that catches a feature rendering wrongly
# (--feature a,b runs a subset; occlusion_culling needs a template with the raycast module, the web default):
cd webgpu_tests/forward_plus && ./export.sh && WEBGPU_REAL_GPU=1 node run_forward_plus.mjs --parity

# Per-frame WebGPU call/byte budgets on the scene smoketest exports (needs a fresh export first, see TASKS.md §3):
cd webgpu_tests/perf_guard && node run_perf_guard.mjs
# Where a CPU-bound export spends its frame (api_profile.mjs: calls/ms per GPU method; cpu_steady.mjs: wasm CPU by function):
node api_profile.mjs --dir <export> --uncapped
# Whether it is GPU-bound instead, and which pass costs what on the GPU (uncapped "100% busy" includes waiting on the GPU):
node gpu_time.mjs --dir <export> --uncapped [--passes]
# JS-glue cost per function (ms/frame) on a tab YOU already have open in a Chrome started with --remote-debugging-port=9222
# (no hosting or launching; written for the Web Audio AudioParam writes, --fn takes any names; usage in its header):
node tests/web_audio_profiling.mjs --tab <url-or-title-substring> --fn setVolume,_syncPlaybackRate

# Load-ramp benchmark -- the source of README.md's performance table: each ramp adds content until 20 fps, on native
# Vulkan/Compatibility, stock WebGL 4.7.2, WebGPU Mobile/Forward+ and Firefox (~63 min, needs an idle real GPU), then
# regenerate the table between README.md's perf-table markers (never hand-edit it):
cd webgpu_tests/benchmark && ./run_ramp.sh && python3 readme_perf_table.py results_ramp/<date> --write

# Full local CI (rebuilds engine + runs everything, mirrors CI):
./webgpu_tests/local_ci.sh                 # rebuild + re-export + full suite
./webgpu_tests/local_ci.sh --quick          # shader corpus + scene smoketest only, no rebuild
./webgpu_tests/local_ci.sh --no-safari      # skip Safari (no AppleScript needed)
./webgpu_tests/local_ci.sh --no-export      # rebuild but keep the existing exports
./webgpu_tests/local_ci.sh --no-dlink       # skip the dlink template compile check
./webgpu_tests/local_ci.sh --dev-mode       # build with dev_mode=yes (werror), as CI does
./webgpu_tests/local_ci.sh --on-desktop     # open the test browsers on your display (default on Linux: its own Xvfb)
./webgpu_tests/local_ci.sh --no-real-gpu    # skip the real-GPU tiers (on by default when Vulkan lists a GPU)
```

`local_ci.sh` builds the editor, the dlink web template (a compile check only — nothing loads it) and the **non-dlink nothreads** one, which with the editor is the pair the scene smoketest exports with. It re-exports every scene from them before testing, so a green run reflects the working tree. It sources `$EMSDK_ENV` (default `~/emsdk/emsdk_env.sh`) itself when `emcc` is not on `PATH`.

**CI builds with `dev_mode=yes`, which implies `warnings=extra werror=yes`; none of the commands above do.** A warning that is fatal in CI is invisible locally — this is how an unused-variable error kept 🧪 WebGPU Tests red for two days while every local tier was green. Run `./webgpu_tests/local_ci.sh --dev-mode` before assuming a local green means CI will agree. Three more breaks only `main`'s 🔗 GHA catches (`webgpu_notes/TASKS.md` §3 trap 112): a new enum on a builtin Variant type such as `Color` breaks godot-cpp (bind an `int` and plain constants instead); `near`/`far` are macros on Windows; and a call into code behind `#ifndef _3D_DISABLED` must be guarded too, or the minimal template fails to link. After engine API or `scene/` changes, build `platform=windows use_mingw=yes target=template_release d3d12=no` and the minimal template (`linux_builds.yml`'s flags) before a PR to `main`.

Main's 📊 Static checks run `prek` over **every file the merge brings**, notes, result JSON and helper scripts under `webgpu_notes/` included, and when they fail no build runs. Before a PR to `main`, run `pre-commit run --from-ref origin/main --to-ref HEAD` with the fixes staged (pre-commit stashes unstaged changes and checks the committed text), `webgpu_notes/TASKS.md` §3 trap 75. A new folder under `tests/` must also be added to the tests unit in `scu_builders.py`, or CI's `scu_build=yes` GCC sanitizer editor fails to link it an hour into the run (trap 74).

On GitHub, `🔗 GHA` (`runner.yml`) builds everything on a push to `main` and drafts a release. The Linux sanitizer editors (ASan/UBSan/TSan, `linux_builds.yml`'s `sanitizers-linux` job) run in the same workflow, but the release does not wait for them, since they ship nothing; the release does ship a Linux `precision=double` editor (`-linux-double.zip`) alongside the three standard editors. To build only an editor, run `🐧 Linux editor`, `🍎 macOS editor`, `🏁 Windows editor` or `🖥️ All editors` (`.github/workflows/editor_*.yml`) from the Actions tab; each uploads the editor zipped by `.github/actions/package-editor`, exactly as a release ships it. Editors must be zipped on the runner that built them — `actions/upload-artifact` drops Unix permissions, which is how every Linux and macOS release up to v1.5 shipped an editor that could not start (§3 trap 41). Keep the manual workflows' flags in step with the release editor jobs in `*_builds.yml`.

On Linux, `local_ci.sh` re-runs itself under `xvfb-run` so the headed test browsers never take focus or cover the desktop. Headed Chrome keeps the real GPU there and its canvas composites; Firefox gets the real adapter and runs (smoketest, WebRTC), but cannot composite a WebGPU canvas on Xvfb, so its three pixel checks (fog, font, screenshot comparison) report SKIP: run them with `--on-desktop`. Headless Chrome gets no WebGPU adapter at all since Chrome 156.0.8078.79. Run other window-opening tiers the same way: `xvfb-run -a -s "-screen 0 1920x1080x24" <command>`.

Every Playwright launch in these tiers must set `headless` explicitly (`headless: isCI`, or `false` for real-GPU runs): Playwright defaults to headless, where Chrome gets only the swiftshader fallback adapter and loses the WebGPU device at the first canvas present — a failure that comes and goes with startup time, which is how `webrtc_parity` bisected to an innocent commit (`webgpu_notes/TASKS.md` §3 trap 43).

SPIR-V dump validation and the smoke/scene-smoketest tiers need a full editor + web template build first (see `webgpu_tests/README.md` for exact commands and `GODOT_DUMP_SPIRV` env var usage). `webgpu_tests/shader_corpus/expected_failures.json` is the baseline of known/accepted conversion failures (Vulkan-only shader variants never used by the WebGPU runtime, each with its reason) — CI only fails on failures *not* in that baseline.

With Tint gone there is no second converter to compare against, so the interface checks compare with **stored references** made from Tint's output when SPIRV-Cross replaced it: `webgpu_tests/spirv_cross_eval/interface_reference.json` (bindings, kinds and override ids of every engine variant, 311 on 2026-10-28, read by `compare_interface.py`) and `webgpu_tests/wgsl_text_scan/scan_reference.txt` (the driver's WGSL text-scan facts, read by `wgsl_text_scan/run_tests.sh`). `webgpu_tests/converter_smoke/run.sh` runs both (`webgpu_notes/work/smoke.sh` forwards to it). They are the only check, short of rendering, that bind-group layouts and pipeline constants still line up. A reference module missing from the candidate fails. After a deliberate interface change, regenerate them (`compare_interface.py --write`, `WRITE_REFERENCE=1 run_tests.sh <run>`) and say why in the commit. Every offline tier that drives the converter takes `WGSL_CONVERT_CLI=<path>` (default `bin/wgsl_convert_cli`).

## Linting

Standard Godot pre-commit setup (clang-format for C/C++/GLSL, ruff for Python, codespell, etc.):

```bash
pre-commit run --all-files
pre-commit run --hook-stage manual clang-tidy   # needs compile_commands.json, not run automatically
```

## Architecture

### The RenderingDeviceDriver pattern

Godot's rendering backends are all implementations of the same abstract interfaces defined in `servers/rendering/rendering_device_driver.h`, `rendering_context_driver.h`, and `rendering_shader_container.h`. When adding or changing a driver method, check `drivers/vulkan/` and `drivers/metal/` first for the reference pattern — Metal is generally the closest architectural analog to WebGPU among the existing backends. **Any change to these three base-class headers requires auditing all pure-virtual overrides in `drivers/webgpu/` for gaps** — a missing override doesn't always show as a compile error if the base class still provides a default, but calling it can silently misbehave or (for pure virtuals) fail to link.

### `drivers/webgpu/` file roles

| File | Role |
|------|------|
| `rendering_device_driver_webgpu.{h,cpp}` | Core driver — implements all `RenderingDeviceDriver` virtual methods (buffers, textures, pipelines, command recording, bind groups, push-constant emulation) |
| `rendering_context_driver_webgpu.{h,cpp}` | Device/adapter/surface lifecycle, driven from the JS shell |
| `rendering_shader_container_webgpu.{h,cpp}` | SPIR-V storage + push-constant metadata for compiled shaders |
| `webgpu_objects.h` | Thin wrapper structs around WebGPU handles (`WGBuffer`, `WGTexture`, `WGShader`, `WGPipelineWrapper`, ...) |
| `pixel_formats_webgpu.h` | Godot `DataFormat` ↔ `WGPUTextureFormat` mapping |
| `spirv_preprocess.{h,cpp}` | Binary-level SPIR-V rewriting passes, and `run_pipeline()` — the single pass order every converter (runtime, `wgsl_convert_cli`, fuzzer) calls (see below) |
| `spirv_opt_passes.{h,cpp}` | Passes written on SPIRV-Tools' optimizer IR because they restructure control flow or add types: `expand_binding_arrays`, `apply_sampler_lod_bias`, `lower_wgsl_unsupported_ops`. Separate file because SPIRV-Tools' internal headers need extra include paths (`env_spirv_opt` in `SCsub`) |
| `spirv_binding_layout.h` | Dependency-free constants shared by the passes and the driver: where expanded texture-array elements bind (400 + 16b + k − 1), and the `SamplerLodBias` binding (40) |
| `spirv_cross_wrapper.{h,cpp}` | Runs SPIRV-Cross's WGSL backend (`thirdparty/spirv-cross-wgsl`) on preprocessed SPIR-V; built with wasm exceptions, because SPIRV-Cross reports an unsupported shader by throwing (the wrapper catches; the rest of the engine stays `-fno-exceptions`). `spirv_to_wgsl.cpp` is the runtime entry point (`run_pipeline()` + this) |
| `wgsl_text_scan.{h,cpp}` | Everything the driver and baker read out of converted WGSL (bind-group layout facts, override ids, texture/sampler pairs) and the three text rewrites, parsed structurally — new code that reads WGSL goes here, never `strstr`. A scan that finds nothing does not fail; the binding silently gets a default. std-only, so `webgpu_tests/wgsl_text_scan/run_tests.sh` runs the same code natively |
| `wgsl_precompile.py` | Build-time driver: compiles every engine shader variant through the same pipeline the runtime uses, ahead of time, into `wgsl_precompiled.gen.h` (generated, gitignored) |
| `wgsl_cli/` | `wgsl_convert_cli` — standalone native host tool wrapping `spirv_preprocess` + `spirv_cross_wrapper` for build-time precompilation, export-time baking and standalone testing/debugging (see Build Commands); `wgsl_cli/fuzz/` holds the libFuzzer harnesses |

### Shader pipeline

```
GLSL → SPIR-V (glslang, at engine/editor build time)
     → spirv_preprocess::run_pipeline() (~26 passes, C++, at runtime or build-time precompile)
     → WGSL (SPIRV-Cross's WGSL backend, C++, linked directly into the engine — no WASM/JS translation step)
     → GPU (browser's WebGPU implementation)
```

SPIRV-Cross's WGSL backend is vendored under `thirdparty/spirv-cross-wgsl` ([Ludicon/spirv-cross-wgsl](https://github.com/Ludicon/spirv-cross-wgsl), separate from Metal's `thirdparty/spirv-cross` and in its own namespace; local patches in its `patches/`, listed in `thirdparty/README.md`, **never upstreamed**). The passes need `thirdparty/spirv-tools` and `thirdparty/spirv-headers` (the latter extracts a broader header set than upstream Godot's own minimal extraction, for SPIRV-Tools). Shaders are precompiled ahead-of-time by `wgsl_precompile.py` during the engine build where possible; the same `run_pipeline()` + SPIRV-Cross chain also runs at runtime (`spirv_to_wgsl.cpp`) as a fallback for anything not precompiled; the full pass order is listed in `webgpu_site/TECHNICAL_REFERENCE.md` §4.1, and `run_pipeline()` itself is the source of truth. The passes exist to work around specific gaps/quirks between what glslang emits and what the converter/WGSL accepts (combined-sampler splitting, push-constant→uniform conversion, depth-texture fixups, texture arrays, etc.) — when a new shader fails conversion, the fix is almost always either a new/extended preprocessing pass here or a SPIRV-Cross patch, not a WGSL text rewrite. Some pass rationales still mention Tint (`lower_wgsl_unsupported_ops`, once `lower_tint_unsupported_ops`, exists because of it): Tint was the converter until 2026-10-25 (`webgpu_notes/TASKS.md` §4.36; the tag `pre-SPIR-V-Cross` marks the tree before the swap). `run_pipeline()` validates its input with SPIRV-Tools' validator (Vulkan 1.2 rules) and rejects a malformed module as a failed conversion (`invalid SPIR-V, not converted: ...`), because SPIRV-Tools' optimizer *asserts* on one (or, under `NDEBUG`, reads through null pointers); only the engine's own runtime conversion skips the full validation, its input being glslang's output. Hand-built test SPIR-V must therefore be valid (`spirv-val --target-env vulkan1.2`).

**Debugging a conversion failure**: `wgsl_convert_cli`'s batch mode (used by `wgsl_precompile.py` and the export-time bake) forks a child process per shader and redirects its stdout/stderr to `/dev/null`, specifically so an abort in a pass or the converter doesn't kill the whole batch — this means the real diagnostic is normally invisible. Run `wgsl_convert_cli <file.spv>` directly (single-file mode, un-forked) to see it; `--spec-info` says which specialization-constant path a module takes, `--raw` skips the passes. Two env-var-gated debug hooks exist for this: `WGSL_DEBUG_DUMP=<substring>` on `wgsl_precompile.py` dumps matching shaders' raw SPIR-V to `/tmp/wgsl_debug_dump/`, and `WGSL_DEBUG_DUMP_PREPROCESSED=<path>` on `wgsl_convert_cli` dumps the SPIR-V after every pass but before SPIRV-Cross sees it (the old name `TINT_DEBUG_DUMP_PREPROCESSED` still works for one release).

### Key architectural decisions

- **Push constants**: not supported by WebGPU — emulated via a ring buffer at group(3)/binding(120), rewritten from `OpVariable PushConstant` at the SPIR-V level.
- **Subpasses**: not supported — each Godot subpass becomes a separate `WGPURenderPassEncoder`. `render_forward_mobile.cpp` disables `using_subpass_post_process` under `WEB_ENABLED`.
- **Barriers**: `command_pipeline_barrier` is a no-op — WebGPU tracks hazards automatically.
- **Pipelines and the GPU process**: Chrome compiles `createShaderModule`/`createRenderPipeline`/`createComputePipeline` on the GPU process's main thread, which also produces the page's frames, so a synchronous compile freezes the page however cheap the call looks (`webgpu_tests/startup_phases/gpu_process_trace.mjs --after <ms>` shows it). Modules and compute pipelines are created on first use, compute pipelines are prewarmed with `createComputePipelineAsync()`, and render pipelines that `PipelineHashMapRD` compiles in the background go through `createRenderPipelineAsync()` (`RD::set_pipeline_creation_async()` / `render_pipeline_is_ready()`): such a pipeline must not be bound before it is ready, and stays out of the map until then. See `drivers/webgpu/README.md` and `webgpu_notes/TASKS.md` §2.4.
- **Specialization constants**: normally left in the SPIR-V so SPIRV-Cross emits `@id(N) override` declarations, which pipeline creation sets through WebGPU pipeline constants — one base module per shader, no runtime re-conversion. `spirv_preprocess::spec_constants_overridable()` decides per module; anything it rejects (non-scalar constants, an `OpSpecConstantOp` the converter cannot lower, spec-constant array sizes/composites/workgroup sizes) is frozen to its defaults by `freeze_spec_constant_ops()` and specializes through the legacy re-patch-and-re-convert path instead. All-or-nothing per shader — see `drivers/webgpu/README.md`. Under SPIRV-Cross 93 of the 276 engine variants keep overrides and none is frozen; the frozen path exists for user shaders, and `freeze_spec_constant_ops()` evaluates composite ops (CompositeExtract/Insert, VectorShuffle) per component, warning and emitting `OpConstantNull` for anything it cannot evaluate, never a silent 0.
- **Buffer mapping**: `buffer_map()` returns a CPU shadow copy (not GPU-visible memory like Vulkan/Metal/D3D12); `buffer_unmap()` flushes it via `wgpuQueueWriteBuffer`. This is why `RenderingDevice`-level code that's driver-agnostic gates WebGPU-only behavior behind `driver->api_trait_get(RDD::API_TRAIT_*)` checks rather than `#ifdef WEBGPU_ENABLED` — see the `API_TRAIT_*` block in `rendering_device_driver.h` for the full list of WebGPU-specific traits and why each exists (they're heavily commented). **Any new cross-driver optimization gated this way must also get a base-class default in `rendering_device_driver.cpp`'s `api_trait_get()`**, or every non-WebGPU driver spams `ERR_FAIL_V(0)` when the shared code queries it. `RDD::buffer_get_readback_status()` (default `OK`) is how shared code asks whether a download's staging buffer can be read yet; RenderingDevice's asynchronous downloads wait on it instead of trusting `fence_wait()`, which on WebGPU force-signals. A synchronous `buffer_get_data()` / `texture_get_data()` on the web returns an empty array until that readback has arrived (`r_not_ready` says so), never zeros: engine code reading a GPU buffer synchronously must treat empty as "no data yet" (`webgpu_notes/TASKS.md` §4.96).
- **Device init**: the JS shell pre-initializes a `GPUDevice` and stores it on `Module["preinitializedWebGPUDevice"]`; C++ retrieves it via the emdawnwebgpu port's `WebGPU.importJsDevice()` through `EM_ASM_PTR`, not `emscripten_webgpu_get_device()`/`html5_webgpu.h` (removed in modern Emscripten).
- **Format constraints**: no 3-component texture formats (RGB8/16/32 unsupported as textures), 256-byte row alignment for buffer↔texture copies, no multi-draw-indirect (loop over individual draws), max 4 bind groups (set 3 is reserved for push-constant emulation).
- **Texture arrays**: WGSL has no handle arrays Chrome accepts, so `expand_binding_arrays` turns each into one binding per element (`spirv_binding_layout.h`) and dynamic indexing into a switch. It is budgeted per stage — Chrome reports exactly the 48 sampled textures Forward+ requires — and arrays past the budget fall back to `flatten_binding_arrays` (element 0, warned once). Don't raise the budget without counting the stage's other textures; over the adapter limit, pipeline creation fails outright.
- **Sampler LOD bias**: WebGPU samplers have none. The scene shaders declare `SamplerLodBias` (set 1, binding 40, filled by `MaterialStorage::Samplers`), and `apply_sampler_lod_bias` adds it to implicit-LOD samples.
- **Depth textures must look like depth textures**: a GLSL `texture2D` becomes WGSL `texture_depth_2d` only if `_reclassify_single_component_depth_textures()` recognizes it — by name (`depth`) and usage. Anything it misses gets a depth texture bound to a float slot, which the driver replaces with a blank fallback, silently (`print_verbose` names the shader, set and binding). Name new depth bindings in renderer shaders with `depth`, and read them linearly through `textureGather` + weights (`textureLod_linear_depth()`), never a linear sampler.
- **Point size** is emulated (`SUPPORTS_POINT_SIZE` false): the scene shaders draw each point as a quad. `POINT_COORD_USED` implies `POINT_SIZE_USED` (scene `_inc.glsl`), or a `POINT_COORD`-only material draws nothing.
- **Parity, not just "it changed"**: the Forward+ matrix's delta test passes a feature that renders wrongly. Judge renderer changes with `run_forward_plus.mjs --parity` (native Vulkan reference), and see `webgpu_notes/TASKS.md` §3 trap 28 for what makes that comparison trustworthy.

### Web audio: Sample playback re-implements AudioServer

Web exports default to **Sample** playback (`audio/general/default_playback_type.web`): the engine renders each
sound once and Web Audio plays it (`GodotSampleDriver` in `platform/web/js/libs/library_godot_audio.js`, fed by
`AudioDriverWeb` in `platform/web/audio_driver_web.cpp`), bypassing `AudioServer`'s mixer, which Stream playback
(and every native platform) uses. So every behavior of `AudioServer::_mix_step()` that a Sample sound should have —
per-channel volume vectors, volume ramps and fades over one 512-frame block, the bus routing rules, loop modes, the
3D high shelf, positions — is re-implemented in that JS, and it is tested against ports of the native code by
`webgpu_tests/web_audio/run_tests.mjs` (offline, sample for sample) and end to end by `run_e2e.mjs` / the smoketest's
`audio_parity` scene. Change either side and run both. Rules that cost time to find (`webgpu_notes/TASKS.md` §4.30,
§3 traps 32 and 34):

- Per-frame updates (3D/2D volumes, pitch, high shelf) are compared in C++ and only cross into JS when they change;
  JS knows streams and playbacks by integer handles, not ObjectID strings. Keep it that way: the JS-side skip alone
  still cost 15x more.
- The driver caches a rendered sample per stream and re-renders it when the stream emits `changed`. A new
  `AudioStream` setter that changes what plays must call `emit_changed()`, or web playback keeps the old sound.
- Sample playback cannot run bus effects or stream long files; it warns (export log; runtime, once per bus/stream).
  The ways to close those gaps are §2.32, deliberately deferred.
- Timing checks in `fixture_project/main.gd` compare with a model of `AudioServer`'s playback at the time that actually
  passed on the audio clock (a silent reference sound started with each measured one), never by subtracting a timer's
  overshoot from a position that may have wrapped or clamped: GitHub's runners fire timers ~1 s late (§4.97, trap 111).
  Natively they only run with a real audio driver (`--display-driver headless --rendering-driver dummy --audio-driver
  PulseAudio`); `--headless` picks the Dummy driver, which skips them.

### Editor: drag-and-drop onto lists goes through one resolver

`editor/inspector/editor_collection_drop.{h,cpp}` turns dragged files/folders, a resource or nodes into values for a
slot type (resources by class, nodes, relative NodePaths, paths honoring file hints and `uid://` for
`@export_file`). `EditorPropertyArray`, `EditorPropertyDictionary`, `EditorInspectorArray` (property arrays) and the
Autoload, Localization, audio bus effect and Animation Library lists all use it; a new list-like UI should too, so
every list accepts the same drops. Test with `tests/editor_drop/run.sh` (real drags in a windowed editor under Xvfb;
a `--headless` editor has a 2x2 viewport and cannot be driven through its UI — §3 trap 33). Details: §4.31.

### Physics: Box3D is the default 3D physics engine

`modules/box3d_physics/` implements `PhysicsServer3D` on top of the official [Box3D](https://github.com/erincatto/box3d) engine (vendored in `thirdparty/box3d`, plain C17). It registers as `"Box3D Physics"` and as the default server at priority 1 (above GodotPhysics3D's 0), so `physics/3d/physics_engine = DEFAULT` resolves to it and new projects created by the editor are written with `"Box3D Physics"`. `"Jolt Physics"` and `"GodotPhysics3D"` stay selectable in the project settings. The module mirrors `modules/jolt_physics/` in structure (`objects/`, `shapes/`, `spaces/`, `joints/`); when changing behavior, check how Jolt does it first, and compare results against Jolt/GodotPhysics3D with a scratch project rather than trusting a single engine.

Key differences from a "normal" backend that are easy to trip over:

- Box3D shapes belong to one body and have their transform baked into their geometry, so every (object, Godot shape) pair gets its own `Box3DShapeInstance`; transform, scale and add/disable changes rebuild the Box3D shapes deferred to the next step (`Box3DSpace3D::flush_pending_shapes()`), while changing a shape resource's data rebuilds every owner at once and removing a shape destroys its Box3D shapes immediately.
- Godot's one-directional layer/mask rules and area monitoring rules cannot be expressed with Box3D's symmetric filter bits, so every shape passes the built-in filter and `Box3DSpace3D::_custom_filter()` makes the real decision. Areas are Box3D sensor shapes on a static body.
- Box3D has no cylinder, plane, soft body or generic 6DOF joint. Cylinders are prisms, world boundaries are huge boxes, and unsupported features go through `Box3DDiagnostics::report_unsupported()`, which prints a one-time warning with everything needed to diagnose it. When you hit a new gap, report it that way instead of failing silently.
- `body_test_motion` and the shape queries do their own narrow phase (`spaces/box3d_query_shape.cpp`) on top of Box3D's public `b3Collide*` functions.
- Web builds always run Box3D single threaded (`workerCount = 1`); its own thread creation cannot work from the web export's fixed pthread pool.

Native validation: build the editor (see Build Commands) and run headless GDScript scenes against each engine (`--headless --path <project> -s test.gd`, switching `physics/3d/physics_engine`).

### MetaHuman importer

`modules/metahuman/` imports a MetaHuman export: `head.dna` imports the whole character.
- **Rig:** OpenRigLogic (`thirdparty/openriglogic`, MIT) evaluates the face and the body correctives in
  `MetaHumanRig`, a `SkeletonModifier3D`.
- **Clothing:** the FBX is rebound to the DNA skeleton.
- **Grooms:** the Alembic grooms become strands, a baked shell, and a guide simulation (`MetaHumanHairSimulator`).

`webgpu_notes/TASKS.md` §2.33 has the formats, the measured numbers and why each choice was made. Test with
`webgpu_tests/metahuman/run.sh <export.zip> [--shots] [--export]`; no export ships in the repo.

Rules that cost time:
- Match DNA meshes to materials by mesh name: the manifest's per-mesh list is wrong past LOD 2 (§3 trap 48).
- DNA is Y-up centimeters; grooms are (x, z, y) centimeters.
- Hair shaders branch on `IN_SHADOW_PASS` and `abs(PROJECTION_MATRIX[1][1])` (traps 50, 51).
- Strand shaders clamp `v_coverage` before `pow()`: lash tips have width 0 and interpolate to -1e-9 (NaN on NVIDIA,
  which the bokeh DOF used to spread into a black hexagon, trap 70). The importer's strand variants replace
  `HAIR_STRANDS_ALPHA_LINE`; change the line and the constant together.
- The shaders are baked into the imported scene, and `head.dna` never changes, so an importer or shader fix reaches an
  existing project only after a Reimport (trap 71).
- The skin's pore normal tiles in UV space, and body UVs are 3.4x coarser than the face's: scale the tiling from the
  mesh (`metahuman/materials/skin_detail_size`), never with a constant (§2.33). Normal maps sample blue as 0 (trap 72).

### Braced GDScript (`.gdb`)

`.gdb` files are GDScript written with `{ }` blocks (plus `//` comments, `else if`, `c ? a : b`, arrow lambdas),
loaded by the same `GDScriptLanguage`. The tokenizer translates braces into the `COLON`/`NEWLINE`/`INDENT`/`DEDENT`
tokens the parser already reads (`gdscript_tokenizer_braces.cpp`); everything after it is shared. User and developer
docs: `docs/braced_gdscript/` (start with `internals.md`). One formatter (`gdscript_formatter.cpp`) serves the editor's
Reformat Code, `--gdscript-format`, `--gdscript-convert`, `--gdscript-format-server` and the language server. Every
upstream change to these files is tagged `// GDB`. Rules that cost time:

- Any `get_extension() == "gd"` in `modules/gdscript` must be `GDScriptLanguage::is_gdscript_path()`, or `.gdb` scripts
  silently miss that code path.
- Test headers are compiled from `tests/test_main.cpp` without the module's defines, so a braced test checks for brace
  mode at run time (`GDScriptFormatter::get_braces_grammar_version()`), not with `#ifdef GDSCRIPT_BRACES_ENABLED`.
- A change to the block-or-dictionary rule or the braced spellings bumps `GDScriptFormatter::BRACES_GRAMMAR_VERSION` and
  the Rider plugin's `GdbPsiLexer.GRAMMAR_VERSION` (and the tree-sitter grammar), which mirror it.
- Test with `tests/braced_gdscript/run.sh` (`--quick` for two minutes); web templates must be built from this tree for
  the `braced_gdscript` smoketest scene. Editor plugins live in `../godot-gdb-rider`, `../godot-gdb-vscode` and
  `../godot-gdb-tree-sitter`.

### Where to look first

- `webgpu_notes/TASKS.md` — **the** task/status doc, and the only one: §1 current state and build traps, §2 outstanding work ordered by criticality with effort estimates, §3 the traps and lessons that have cost time, §4 the archive of completed phases, §5 what the retired notes docs were and how to read them out of git history. Read §1-§3 when picking the work up cold. Keep it updated: move an item out of §2 into §4 when it closes, and add a §3 entry for anything that cost an hour and would again.
- `webgpu_site/ARCHITECTURE_AND_DESIGN.md`, `TECHNICAL_REFERENCE.md`, `PERFORMANCE_AND_OPTIMIZATION.md`, `CORRECTNESS_AND_COMPATIBILITY.md` — in-depth docs on the areas their names suggest.
- `webgpu_site/RAY_TRACING_DESIGN.md` — ray and path tracing on WebGPU Forward+: software BVH traversal by necessity (WebGPU has no hardware RT), the hybrid tiers (RT AO and shadows first, desktop-class opt-in, off on mobile), and the full path tracer saved for later (§7). Measured by `webgpu_tests/raytracing_probe/` (desktop 1.9–2.3 Grays/s incoherent, Razr 60 19 Mrays/s); status in `webgpu_notes/TASKS.md` §2.34.
- `drivers/webgpu/README.md` — driver-local notes.
- **Editor update check**: `EngineUpdateLabel` asks this fork's GitHub releases (`FORK_RELEASES_REPO`), not
  godotengine.org; a release is newer when its `main-<sha>` commit is not the build's and was committed later
  (`webgpu_notes/TASKS.md` §4.45). Keep the release workflow's `main-<sha>` tags and `godot-webgpu-*-<version>` asset
  names, or the check finds no release.
- **Project Boards**, https://project-boards.shanegadsby.com/#/godotwebgpu — the live task board and every question for the user (the user's own server on 192.168.1.3, shared with other projects). CLI `/media/media-server-storage/servers/project-boards/bin/pb`; `pb inbox` for answers; `pb settings` for `maxSubagents` (never exceed it). Session instructions: https://project-boards.shanegadsby.com/onboard/godotwebgpu. See `webgpu_notes/work/README.md` "The board".
- **The wiki**, https://github.com/Shane-Gadsby/godotwebgpu/wiki — the user documentation: Braced GDScript (7 pages), Box3D Physics (6), Engine Additions (12). Local clone `/mnt/109313D2109313D2/godot-editors/godotwebgpu.wiki`, a separate git repo (branch `master`) pushed on its own; the user allows pushing it, unlike this repo. User-facing docs for those three areas go there; `docs/braced_gdscript/` stays the developer reference. `README.md` links into it.
- `webgpu_tests/screenshot_comparison/LIVE_REPRO_METHODOLOGY.md` — how to test a specific reported bug against a real Godot project (not this repo's own fixtures): scratch-copy setup, native-Vulkan screen-recording capture, WebGPU screenshot/console capture via Playwright, export-freshness verification, and reading numeric data back out of the engine for non-visual comparisons. Use this before improvising a one-off repro script.

## Godot Coding Conventions

- C++ style: `snake_case` everywhere; classes `PascalCase`; member variables `_prefixed` or plain `snake_case`.
- Memory: `memnew(T)` / `memdelete(ptr)`, not `new`/`delete`. `Vector<T>` / `HashMap<K,V>`, not STL containers, in engine code.
- Error handling: return `ERR_*` constants; use `ERR_FAIL_COND_V(cond, val)` / `ERR_FAIL_NULL_V(ptr, val)` macros rather than manual `if` + return.
- Logging: `print_verbose(...)`, `WARN_PRINT(...)`, `ERR_PRINT(...)` — never `printf`/`std::cout` in engine code (the native `wgsl_cli` host tool is an exception, since it's a standalone binary outside the engine runtime).
  **On web, pick the severity deliberately**: `WebLogger` (`platform/web/os_web.cpp`) is what makes the distinction visible — it routes `WARN_PRINT` to the page's `onPrintWarning` callback (`console.warn` by default) and leaves every error type on stderr, which Emscripten hands to the shell's `printErr` and every shell logs with `console.error()`, wasm backtrace attached. Before that logger existed there was no warning channel at all, so a warning about behavior that is correct by design (the first result of an asynchronous readback) read as a crash in the player's console and was reported as one. State-of-the-world messages belong at `print_verbose`, which in an export needs `--verbose` added to `GODOT_CONFIG['args']` in the HTML. See `webgpu_notes/TASKS.md` §4.28 and §3 trap 27.
- Opaque driver IDs are integers cast to/from pointers via the `ID` helper — see existing RDD methods in `drivers/webgpu/` for the pattern.
- Includes: `"..."` for project headers, `<...>` for system/third-party; no `.h` extension for standard library headers.
