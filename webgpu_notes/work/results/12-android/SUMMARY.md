# Task 12: Razr 60, Chrome for Android, SPIRV-Cross vs Tint, 2026-10-25

**State: ready-for-integration.** Scope (user): Chrome for Android only; Firefox for Android out of scope, nothing
installed. Phone: Razr 60 (Mali-G615 "valhall", Chrome 155), Forward+ (adapter reports 48 sampled textures).
Worker-12 found and bisected the bug and wrote the fix; worker-12b measured it on the phone and ran the desktop checks.

## Fog fix: SPIRV-Cross patch 0019 (no WGSL `for` statements)

Before the fix, fp_check `volumetric_fog_1` on the phone with SPIRV-Cross was 12.555 mean abs diff from native Vulkan
(Tint 0.176, desktop SPIRV-Cross 0.001). The heavy scene lost its fog the same way. Worker-12 bisected it on the device
by swapping WGSL modules: it is the VolumetricFogProcess filter variant. SPIRV-Cross emitted
`for (var i = -3; i <= 3; i++)` there, and on Mali that loop runs zero times. Every `loop {}` or `while` form of the same
loop renders correctly, and so does a `for` loop counted from 0.

The fix is `ControlFlowSyntax::support_for_loop`, set false for WGSL. Both `ForLoop` cases of `attempt_emit_loop_header()`
fall back to the generic loop, so SPIRV-Cross never emits a WGSL `for` (the shape Tint always emitted). It changes
`thirdparty/spirv-cross-wgsl/spirv_glsl.{hpp,cpp}` and `spirv_wgsl.cpp`, and is recorded as
`patches/0019-no-for-statements.patch`. The `thirdparty/README.md` line is in `for-docs.md`, for the lead to apply.

## Look on the phone (fp_check Look set, 17 configs × off/on, 640x360, against desktop Vulkan)

The full table is in `phone_look_sc19.md`. **All 34 frames are identical between SPIRV-Cross+0019 and Tint (0.000).**
- `volumetric_fog_1`: 0.176 for both (it was 12.555 before 0019). Frames are in `looks/fp_volumetric_fog_1.razr60-*`.
- The worst frames against native are the same for both converters: fsr2_1 0.541, sdfgi_1 0.307, ssil_1 0.290 and
  **subsurface_scattering_1 11.098**. That last one is a phone-vs-desktop difference that Tint has too, so it is not a
  swap regression. See for-tasks.md.
- I regenerated the native references with the current fixture (JITTER_PERIOD 32, lead's editor 14:17 build,
  `native_ref32`). They are pixel-identical to worker-12's (no frame differs by more than 0.001), so the numbers stand.

## Timing on the phone: 3 rotated runs, cool phone before every launch (battery ≤ 36 C, Mali cooling 0)

GPU ms/frame comes from timestamp queries. Values are mean ± half range (median). Full table: `phone_timing_sc19.md`.

| scene | renderer | GPU ms Tint | GPU ms SC+0019 | fps Tint | fps SC+0019 |
|---|---|---|---|---|---|
| fp (Forward+ fixture) | Forward+ | 16.9 ± 0.1 (16.9) | 16.8 ± 0.1 (16.8) | 57.8 | 57.9 |
| heavy | Forward+ | 73.8 ± 1.6 (74.1) | **71.3 ± 0.6 (71.3)** | 13.4 | 13.9 |
| benchmark_postfx | Forward Mobile | 23.3 ± 0.7 (22.9) | 23.2 ± 0.3 (23.3) | 32.8 | 30.9 (frame ms medians equal, 33.2 / 33.3) |
| platformer | Forward+ | 29.6 ± 0.7 (29.2) | 28.7 ± 0.5 (29.0) | 25.5 | 25.4 |
| stress (platformer + inject) | Forward+ | 28.8 ± 0.8 (29.3) | 28.1 ± 0.3 (28.2) | 24.2 | 25.8 |

**SPIRV-Cross+0019 is as fast as Tint or faster on every scene.** Heavy is 3.4% faster, the rest are within spread. Boot
times match except fp, which boots in 17.1 s with SC and 20.5 s with Tint; the fp export translates 97 stages at runtime
on both sides (task 07's bake gap). Error counts are the same for both converters: heavy 3 per run, and postfx 6 per run
from its missing `benchmark_profiler.gd` autoload, which was broken before the swap. No device loss.

## Desktop checks on 0019

- `smoke.sh`: SMOKE OK, 276/276 interfaces identical, 0 invalid. WGSL is 1.093x Tint's bytes (it was 1.063x without
  0019). There are 0 `for` statements left in the output.
  ```
  identical interface: 276/276, mismatched: 0, missing in candidate: 0
  WGSL bytes: reference 6287157, candidate 6870429 (1.093x)
  validation: 276 modules, 0 invalid, 0 of them expected
  SMOKE OK (smoke-20261025-161452-1063099)
  ```
- `project_eval.py` on the 2566 unique real-project stages (the union of `scratchpad/corp/*/spv`): both converters
  convert 2566/2566. `compare_interface.py` gives identical interface 2566/2566. `validate_wgsl.mjs` in Chrome (4080 SUPER)
  gives 2566 modules, 0 invalid. WGSL is 1.121x Tint's bytes, and 0 `for` statements remain.

## Phone state

`stay_on_while_plugged_in` is **restored to 0**. Leases `phone` and `gpu` are dropped. The harness leaves no engine tab
open (each page navigates to `/__done`), and none of the user's tabs were touched.

## What the lead must still run

- Apply the `for-docs.md` line to `thirdparty/README.md`.
- The web templates must be rebuilt after the merge. The phone numbers above come from worker-12's template at
  d1e5a67476 with 0019 applied (`../godotwebgpu-12-tint`, a temporary worktree; it can be removed).
- The Forward+ `--parity` matrix on desktop with 0019 (it changes every module's text). `smoke.sh` and the project corpus
  are valid, but nothing on desktop has been rendered with 0019.

## Risks

- 0019 is broader than the bug: it removes every `for`, not only the ones counted from below zero. That matches Tint's
  shape and measured no slower on the phone. WGSL grows by about 3%.
- The phone shows one converter-independent Look gap, subsurface_scattering_1 at 11.1.

## Files on work/12

- `thirdparty/spirv-cross-wgsl/spirv_glsl.{hpp,cpp}`, `spirv_wgsl.cpp`, `patches/0019-no-for-statements.patch`.
- `webgpu_tests/perf_guard/android/phone_bench.mjs` (worker-12's improvements: thermal gates, `--swap-wgsl`, ...) and
  `phone_summary.mjs`.
- `results/12-android/`: `run_phone.sh` (default configs now tint,sc19), `export_all.sh`, `phone_look_sc19.md`,
  `phone_timing_sc19.md`, `looks/`, `for-docs.md`, `for-tasks.md`.
- Scratch (not in git): `/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/06ad18c7-2f4b-4e67-91c1-7151308515f2/scratchpad/w12/`
  (`exp/{tint,sc,sc19}`, `p19/{sweep,timing}`, `native_ref32`, `peval19`, `smoke19`).

---

# Earlier (worker-12, harness description, before the phone was attached)

## What is ready

- **Exports** (not in git, ~0.7 GB) at
  `/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/06ad18c7-2f4b-4e67-91c1-7151308515f2/scratchpad/w12/exp/{tint,sc}/`:
  `fp` (Forward+ fixture), `heavy` (scene_i_forward_plus_heavy), `benchmark_postfx` (Forward Mobile, task 08's +2%),
  `demo_3d_platformer` (the platformer, and with the smoketest's stress inject, the stress scene). All baked
  (`BAKE=1` / `SMOKETEST_BAKE=1`, `WEBGPU_BAKE_CACHE=0`), with release nothreads non-dlink templates:
  - `sc`: the lead's editor (e99b18555e) next to the lead's SPIRV-Cross `bin/tint_convert_cli`, and the lead's template.
  - `tint`: the same editor next to the lead's `bin/tint_convert_cli.tint`, and worker-11's Tint-everywhere template
    from `../godotwebgpu-11-tint` (built with `webgpu_wgsl_backend=tint` and `WGSL_BACKEND=tint`). Checked: both
    templates carry `e99b18555e98f91a3ef8112721ec0181a70d6ec2`. The Tint one's precompiled table has Tint-style WGSL
    (`main_inner`, `tint_symbol`) and the SPIRV-Cross one has none. I started my own Tint build, found worker-11
    already building the same one, and copied theirs.
  - shaders.pck sizes, Tint / SC: heavy 34.3 / 39.5 MB (as task 08), fp 29.6 / 34.8, platformer 70.8 / 76.0,
    postfx 7.0 / 6.2.
  - `export_all.sh <tag>` (this folder) rebuilds a set.
- **`webgpu_tests/perf_guard/android/phone_bench.mjs`** (new). It needs no CDP, so it drives **Firefox for Android**
  as well as Chrome (the folder's other tools are Chrome/CDP only). The measurement script is injected into the served
  `index.html` and reports back over `fetch()`. The browser is started with a VIEW intent. Each launch records: boot
  time, adapter, renderer banner, fps and mean / p50 / p90 / p99 frame ms over the window, GPU ms/frame per command
  buffer from timestamp queries (when the browser has `timestamp-query`), how many shaders were baked vs translated,
  every error and device loss, and an optional screencap (`-d` display id, the foldable trap).
  - The config order rotates each run, as in task 08's compare.sh.
  - Before each launch it waits for the battery to be under 36 C and the thermal status to be 0. It gives up after
    10 min rather than measure a hot phone.
  - It is resumable.
  - After reporting, the page navigates itself to `/__done`, so a restored tab holds no engine. It never closes the
    user's tabs (the older scripts in that folder close every tab).
  - `--sweep look|all|<ids>` renders fp_check configs at 640x360 with devicePixelRatio forced to 1. It reads each
    frozen frame back with `canvas.toBlob()` and saves it, so the frame compares pixel for pixel with the native
    reference.
  - `--local <browser binary>` runs the same harness on the desktop.
- **`phone_summary.mjs`** (new): the table (mean ± half range, median). For sweeps it also gives each frame's mean
  absolute difference against the native reference and between the two configs.
- **Native Vulkan reference** for the Look set (17 fp configs × off/on, 640x360, settle 240, fixed 60 fps) at
  `.../scratchpad/w12/native_ref/`. Six of them are in `looks/` as `fp_<id>_1.vulkan-desktop.png` for the Look strip.
  To regenerate: `godot.linuxbsd.editor.x86_64 --path webgpu_tests/forward_plus/godot/fp_check --rendering-driver
  vulkan --resolution 640x360 --fixed-fps 60 -- --fp-matrix=<dir> --fp-settle=240 --fp-only=<ids>`.

## Harness check on the desktop (these are not phone results)

Run on the desktop with `--local` under xvfb, holding the `gpu` lease 14:17 to 14:26. The sweep covered ambient_light,
ssao and volumetric_fog. Values are the mean absolute difference (0-255) against native Vulkan; the parity tolerance
is 0.05:

| frame | Chrome 156 sc | Chrome tint | sc vs tint | Firefox 157 (Playwright) sc | Firefox tint | sc vs tint |
|---|---|---|---|---|---|---|
| ambient_light_0 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| ambient_light_1 | 0.002 | 0.002 | 0.000 | 0.002 | 0.002 | 0.000 |
| ssao_0 | 0.002 | 0.002 | 0.000 | 0.002 | 0.002 | 0.000 |
| ssao_1 | 0.008 | 0.008 | 0.000 | 0.012 | 0.012 | 0.000 |
| volumetric_fog_0 | 0.002 | 0.002 | 0.000 | 0.002 | 0.002 | 0.000 |
| volumetric_fog_1 | 0.001 | 0.001 | 0.000 | 0.001 | 0.001 | 0.000 |

So `toBlob()` readback of the frozen WebGPU frame works in both browser engines. Both picked Forward+ on the real
adapter.

The timing path (reports, resume, summary) also ran end to end, on swiftshader with postfx and both configs. Those
numbers mean nothing, because swiftshader loses the device at the first present (trap 43).

The fp export translates 97 stages at runtime (426 baked). That is task 07's known bake gap, and it is the same on
both sides.

Known noise in benchmark_postfx: the project's autoload `res://benchmark_profiler.gd` is missing from
`webgpu_tests/benchmark/godot/`, which costs 3 errors per launch in both configs. It was already broken before the
swap.

