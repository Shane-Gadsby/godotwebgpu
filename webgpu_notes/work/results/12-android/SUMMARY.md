# Task 12: Razr 60, Chrome and Firefox for Android, SPIRV-Cross vs Tint, 2026-10-25

**State: blocked (phone not attached).** `adb devices` was empty at 14:10 (lead), 14:08, 14:16, 14:27 and 14:28.
Everything that does not need the phone is done: both export sets, a phone harness that works in either browser,
desktop Vulkan references, and a one-command phone session (`run_phone.sh`). No phone number exists yet, so nothing
here says anything about Android performance.

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

## What the next worker (or the lead) runs once the phone is attached

```
webgpu_notes/work/results/12-android/run_phone.sh          # takes the phone lease itself; resumable
```

(Relink `webgpu_tests/{forward_plus,scene_smoketest}/node_modules` from the lead's checkout first if the worktree is
fresh; export_all.sh needs them, run_phone.sh does not.)

What run_phone.sh does:
1. Checks which browsers are installed on the phone.
2. Runs one smoke launch per scene and browser, with screenshots.
3. Runs the Forward+ Look sweep in each browser and config, against the native reference.
4. Runs 3 timing runs: 5 scenes × browsers × 2 configs × 3 = 60 launches, ~45 s each plus cool-down. Allow ~1.5 h.

After that:
- Pick looks from `<out>/smoke/shots/` and `<out>/sweep/frames/`, and put them beside `looks/*.vulkan-desktop.png`.
- Compare SC with Tint using the summary tables.
- Where a scene is slower under SPIRV-Cross:
  1. Find the pass with `gpu_time.mjs --passes` (Chrome, over CDP) on that export.
  2. Confirm the WGSL is the cause with `--dump-wgsl` / `--replace-pair`, swapping in Tint's module.
  3. If it is a const-array shape, try `results/08-gpu-timing/lut-var-private.candidate.patch`.

## Risks / open

- **Firefox for Android may not expose WebGPU.** This folder's README (written 2026-10-17) says Firefox for Android
  had none. Release Firefox on Android has no `about:config`. If `navigator.gpu` is missing, the harness reports
  `fatal: navigator.gpu is undefined` for both configs, and only Firefox Nightly (with `dom.webgpu.enabled`) could be
  measured. That would be a gap in the browser, not in the swap (question in for-tasks.md).
- The Look sweep covers 17 of fp_check's 71 configs. `--sweep all` runs every one (~30 min per browser and config).
- Nothing has been measured on the phone yet. Task 08's desktop result is the only performance evidence so far.

## What changed on work/12

- `webgpu_tests/perf_guard/android/phone_bench.mjs`, `phone_summary.mjs` (new).
- `webgpu_notes/work/results/12-android/`: this file, `run_phone.sh`, `export_all.sh`, `for-tasks.md`, `looks/`.
- No change to the converter, the driver or any shared doc.

smoke.sh (this tree):

```
identical interface: 276/276, mismatched: 0, missing in candidate: 0
WGSL bytes: reference 6287157, candidate 6681762 (1.063x)
/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/06ad18c7-2f4b-4e67-91c1-7151308515f2/scratchpad/w12/smoke/smoke-20261025-142712-2523502/wgsl: 276 files, 0 invalid
validation: 276 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261025-142712-2523502)
```
