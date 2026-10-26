# Task 08: GPU time, SPIRV-Cross vs Tint WGSL, desktop Chrome on the 4080 SUPER, 2026-10-25

**Conclusion against the bar: SPIRV-Cross is as fast as Tint on desktop Chrome, and a little faster in the heavy Forward+
scene.** Heavy Forward+: GPU 1.43 vs 1.47 ms/frame (−3%), the same in baked and unbaked exports. The SDFGI passes are
0.015 to 0.02 ms faster each. The four perf_guard scenes (Forward Mobile projects) are equal within noise, except
benchmark_postfx: +0.014 ms/frame summed over its passes (+2%, 0.607 vs 0.593, ranges don't overlap). That is a lead
for task 12, since Android runs Forward Mobile. No patch from this task (see "SSIL check").

## Method (tasks 11 and 12 can reuse it)

- **One editor+template pair per converter, nothing else differs.** Editor: copy of the lead's
  `bin/godot.linuxbsd.editor.x86_64` (62a570d31f). Next to it in `bin/sc/` is the lead's `bin/tint_convert_cli`
  (SPIRV-Cross), and next to a second copy in `bin/tint/` is `bin/tint_convert_cli.tint`. The editor runs the converter
  from its own directory.
  Templates (release nothreads, non-dlink): the lead's (62a570d31f, runtime converter SPIRV-Cross, SPIRV-Cross
  precompiled table), and **a Tint-runtime template I built from a detached worktree at exactly 62a570d31f**
  (`webgpu_wgsl_backend=tint`, with `WGSL_BACKEND=tint` in the environment, build lease, capped scope, 1 min with
  ccache). The commit has to match exactly: the baked cache is keyed by the engine's version hash, and a template one
  commit off loads `baked: 0`.
  So "tint" = Tint-baked + Tint template (Tint precompiled table + Tint runtime), and "sc" = SPIRV-Cross everywhere.
- **Runtime-converted stages:** the heavy scene baked every stage it used (`baked 762, translated 0`), so the ~97-stage
  gap from task 07 does not apply to it. The perf_guard scenes translated 20 to 30 stages at runtime, through each
  config's own template, so each side used only its own converter. **Unbaked exports** (every stage runtime-converted
  by that template) were measured as well, for heavy.
- Exports: `BAKE=1 WEBGPU_BAKE_CACHE=0` with `forward_plus/export_heavy.sh` and
  `SMOKETEST_BAKE=1 run_scenes.mjs --export-only` for benchmark_{postfx,shadows,instances,pbr}
  (`GODOT_EDITOR_BIN`/`GODOT_TEMPLATE_ZIP` set). Heavy shaders.pck: Tint 34.3 MB, SPIRV-Cross 39.5 MB.
- **`compare.sh <out> <runs> <scenes> -- tag=exports ...`** (this folder) rotates the order of the configs each run.
  For every (run, scene, config) it does two `gpu_time.mjs --uncapped` launches: per command buffer (GPU ms/frame,
  fps) and `--passes --rounds 4` (per pass).
  Before every launch it checks: no SCons, load, VRAM, GPU utilization, and no Oblivion/LM Studio in nvidia-smi.
  A failed check makes it wait. A game that starts mid-launch marks that log CONTAMINATED, and the summarizer drops it.
  The script resumes where it stopped. **`summarize.mjs <out>`** prints mean ± half range plus the median.
  `gpu_time.mjs` now prints the adapter, so a log taken on swiftshader is recognizable.
  For Firefox/Android: the gpu_time.mjs launch is Chromium/Playwright only, so tasks 11/12 need their own launcher
  (raw CDP for the Razr). The rotation, the prechecks and the log format carry over.
- Runs: 3 per config per scene (heavy baked: 5), one session 13:00 to 13:55. Adapter `nvidia/lovelace RTX 4080
  SUPER, maxSampledTexturesPerShaderStage 48` in every log (heavy runs Forward+). Background during the session:
  moonlight + gnome-shell at 10 to 40% GPU utilization (the user's desktop), the same for all configs. An earlier
  session at 12:26 was **discarded**: Oblivion Remastered started during it (80% SM).
- **Noise:** about 1 launch in 10 lands in a slow GPU state (half the fps, all passes ~15% slower). That one launch
  makes the large ± values below (tint cb 1.63 ± 0.41, sc passes 1.91 ± 0.46), so read the medians. Passes under
  0.06 ms/frame move ±0.01 ms between launches of the *same* export (SSIL went from 0.040 to 0.060), because each
  launch's window falls at a different moment of the scene's animation.

## Heavy Forward+ (`scene_i_forward_plus_heavy`), baked

| config | runs | fps | GPU ms/frame (cb) | median | sum of passes | median |
|---|---|---|---|---|---|---|
| tint | 5 | 220 ± 66 | 1.630 ± 0.410 | 1.470 | 1.778 ± 0.025 | 1.780 |
| sc | 5 | 247 ± 3 | 1.430 ± 0.000 | 1.430 | 1.912 ± 0.460 | 1.730 |
| sc + LUT patch (candidate) | 5 | 243 ± 4 | 1.422 ± 0.005 | 1.420 | 1.724 ± 0.010 | 1.730 |

| pass (ms/frame) | tint | sc | sc + LUT patch |
|---|---|---|---|
| SdfgiIntegrate | 0.424 ± 0.013 | 0.439 ± 0.114 | 0.392 ± 0.007 |
| SceneForwardClustered (color 1280x720) | 0.206 ± 0.003 | 0.233 ± 0.069 | 0.206 ± 0.002 |
| SdfgiDirectLight | 0.151 ± 0.003 | 0.150 ± 0.039 | 0.136 ± 0.002 |
| VolumetricFogProcess | 0.113 ± 0.002 | 0.118 ± 0.028 | 0.107 ± 0.001 |
| Copy | 0.099 ± 0.001 | 0.107 ± 0.024 | 0.097 ± 0.001 |
| Gi | 0.090 ± 0.001 | 0.099 ± 0.026 | 0.088 ± 0.000 |
| SceneForwardClustered (shadow 4096²) | 0.086 ± 0.001 | 0.097 ± 0.026 | 0.084 ± 0.002 |
| SsilBlur | 0.083 ± 0.001 | 0.090 ± 0.021 | 0.082 ± 0.001 |
| SceneForwardClustered (1280x720 fmt22) | 0.051 ± 0.001 | 0.056 ± 0.014 | 0.050 ± 0.000 |
| Ssil | 0.045 ± 0.000 | 0.052 ± 0.010 | 0.052 ± 0.003 |
| Ssao | 0.041 ± 0.012 | 0.046 ± 0.008 | 0.046 ± 0.006 |
| Tonemap | 0.027 ± 0.001 | 0.031 ± 0.008 | 0.027 ± 0.001 |

The sc column's ± values come from its one slow-state launch. sc + LUT patch shares sc's non-SSIL WGSL, so it is the
cleaner reading of SPIRV-Cross there.

## Heavy Forward+, unbaked (every stage converted at runtime by that template's converter)

| config | runs | fps | GPU ms/frame (cb) | sum of passes |
|---|---|---|---|---|
| tint | 3 | 240 ± 4 | 1.473 ± 0.005 | 1.773 ± 0.010 |
| sc | 3 | 244 ± 3 | 1.430 ± 0.000 | 1.720 ± 0.010 |

| pass (ms/frame) | tint | sc | sc − tint |
|---|---|---|---|
| SdfgiIntegrate:0 | 0.322 ± 0.001 | 0.305 ± 0.003 | −0.017 |
| SceneForwardClustered:18 (color) | 0.203 ± 0.001 | 0.206 ± 0.000 | +0.003 |
| SdfgiDirectLight:1 | 0.152 ± 0.003 | 0.137 ± 0.001 | −0.016 |
| SdfgiIntegrate:1 | 0.107 ± 0.010 | 0.087 ± 0.001 | −0.019 |
| SceneForwardClustered:0 (shadow) | 0.086 ± 0.001 | 0.082 ± 0.001 | −0.005 |
| VolumetricFogProcess:6 | 0.068 ± 0.000 | 0.061 ± 0.001 | −0.007 |
| Ssil:0 | 0.045 ± 0.001 | 0.051 ± 0.002 | +0.006 (noise, see below) |
| Ssao:0 | 0.037 ± 0.002 | 0.042 ± 0.001 | +0.005 (noise, see below) |

## perf_guard scenes (Forward Mobile projects), baked, 3 runs each

| scene | tint GPU ms/frame (cb / passes) | sc | sc + LUT patch | fps tint / sc |
|---|---|---|---|---|
| benchmark_postfx | 0.470 ± 0.000 / 0.593 ± 0.005 | 0.477 ± 0.005 / 0.607 ± 0.005 | 0.473 / 0.610 | 558 / 563 |
| benchmark_shadows | 0.270 ± 0.010 / 0.287 ± 0.005 | 0.273 ± 0.005 / 0.287 ± 0.005 | 0.273 / 0.287 | 829 / 827 |
| benchmark_instances | 0.093 ± 0.005 / 0.100 | 0.090 / 0.100 | 0.090 / 0.100 | 852 / 852 |
| benchmark_pbr | 0.130 ± 0.015 / 0.150 | 0.137 ± 0.020 / 0.150 | 0.140 / 0.150 | 972 / 947 ± 22 |

benchmark_postfx: the +0.014 ms is spread over the Forward Mobile scene passes. The depth/shadow pass to the 4096²
atlas is 0.068 vs 0.064, and the 1280x720 scene+sky pass 0.052 vs 0.050. Not chased further here (2% on a
0.6 ms frame on desktop). On the Razr 60, where Forward Mobile is the renderer, the same passes are the ones to watch
(task 12).

## SSIL check (from task 06)

SPIRV-Cross emits SSIL's 32-entry sample table as module-scope `const _549 : array<vec4f, 32>`, indexed at runtime and
read twice per tap (`_549[p_tap_index]` and `_549[p_tap_index].xy`). Tint emitted `var<private> indexable_1` and read
it once. I wrote a SPIRV-Cross patch that emits every LUT array as `var<private>` (it keeps `const` where a constant
expression needs the value). It changes 44 of 276 engine stages and 410 of the 2566 real-project stages. All 2566
valid in Chrome, interfaces identical to Tint's, smoke OK.
**Measured: it changes nothing.** SSIL is 0.052 with and without the patch.
Then I swapped Tint's SSIL and SSAO WGSL into the SPIRV-Cross export (`gpu_time.mjs --replace-wgsl`, 6 launches each,
alternating): SPIRV-Cross WGSL 0.040 to 0.058 ms (mean 0.048), Tint's WGSL 0.042 to 0.060 (mean 0.050), the Tint export
0.046 to 0.049. So the SSIL/SSAO gap in the tables above does not come from the WGSL. It is within the per-launch
spread of passes this small. A runtime-indexed module `const` costs no more than `var<private>` on Chrome/Vulkan/NVIDIA.
The patch is **not** applied. It is kept as `lut-var-private.candidate.patch` for tasks 11/12 to try where Dawn's
backend differs (Firefox's wgpu/naga, Android's Vulkan driver).

## What changed on work/8

- `webgpu_tests/perf_guard/gpu_time.mjs`: one log line with the adapter (vendor/architecture/description,
  maxSampledTexturesPerShaderStage).
- `webgpu_notes/work/results/08-gpu-timing/`: `compare.sh`, `summarize.mjs`, `lut-var-private.candidate.patch`,
  `logs.tar.gz` (every gpu_time log of the session and the bisect), `for-docs.md`, `for-tasks.md`.
- No converter or driver change.

smoke.sh (this tree, after the patch was reverted):

```
identical interface: 276/276, mismatched: 0, missing in candidate: 0
WGSL bytes: reference 6287157, candidate 6681762 (1.063x)
.../wgsl: 276 files, 0 invalid
validation: 276 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261025-140038-2357503)
```

## For the lead

- Nothing to integrate except the gpu_time.mjs adapter line and the harness.
- Rerun after a SPIRV-Cross patch that changes control flow or adds helper functions:
  `compare.sh <out> 3 heavy benchmark_postfx -- tint=<tint exports> sc=<sc exports>`, then `summarize.mjs`.
  It takes about 15 minutes under the gpu lease. To build the Tint pair again: a template at the editor's exact
  commit with `WGSL_BACKEND=tint scons ... webgpu_wgsl_backend=tint` (see for-tasks.md).
- Risk: one GPU (Lovelace) and one browser. Driver-level codegen on Mali/Adreno (Razr) can react differently to the
  same WGSL shapes (double LUT indexing, the stage-I/O private-variable wrapper), so tasks 11/12 need their own
  readings.
