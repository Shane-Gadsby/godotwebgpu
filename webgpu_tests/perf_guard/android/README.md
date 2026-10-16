# Profiling a WebGPU export on an Android phone (Chrome) over USB

First used 2026-10-17 on a Motorola Razr 60 (Mali-G615 MC2); results in `webgpu_notes/TASKS.md` §2.9.
Node 22+ (global `WebSocket`, `fetch`), `adb` with the phone authorized for USB debugging, Chrome open on the phone.

```bash
adb reverse tcp:8099 tcp:8099     # the phone loads http://localhost:8099/ = a secure context, so WebGPU works without HTTPS
node probe.mjs      --dir <export>                                  # adapter/limits, which renderer the engine picked, fps, console
node fps_matrix.mjs --root <dir of exports> --variants a,b,c        # cold-boot fps / frame-time percentiles / main busy % per export
node gpu_time.mjs   --dir <export> [--passes [--top 40]]            # GPU timestamp queries: ms per command buffer, or per pass
python3 -I wasm_memory_kind.py <file.wasm>...                       # shared (pthreads) vs own memory: which template an export really is
python3 -I pck_diff.py a/index.pck b/index.pck                       # what differs between two exports' pcks
```

Each script serves the export on 127.0.0.1:8099 with COOP/COEP, force-stops Chrome, opens a blank page, closes every
other tab (Chrome restores old tabs; a second engine tab shares the GPU), installs instrumentation with
`Page.addScriptToEvaluateOnNewDocument`, then navigates to `index.html`. `cdp.mjs` is a minimal raw-CDP client:
Playwright's `connectOverCDP` is not needed. The `adb forward` to `localabstract:chrome_devtools_remote` is re-bound
inside the scripts because it dies with every force-stop.

Measure exports with `variant/separate_render_thread=false`. With the separate render thread the engine's WebGPU work
is in a pthread worker that never answers `Runtime.evaluate` (idle pool workers sit in `Atomics.wait`; the render
thread never idles), so the page-side instrumentation sees nothing: ~12% main-thread busy and zero GPU calls means
"the worker is rendering", not "idle". `worker_gpu.mjs` is the unfinished self-driving in-worker variant for that case.

Screenshots on a foldable: `adb exec-out screencap -d <display id> -p` (`dumpsys SurfaceFlinger --display-id`);
without `-d` the output is a text warning, not a PNG.

## Bisecting a slow shader on the phone

`gpu_time.mjs --dump-wgsl <label substring> --dump-dir D --dump-only` saves every distinct module whose label matches
(one per material version — they share a label). Edit copies and substitute them with
`--replace-pair D/<orig>.wgsl=<edited>.wgsl` (exact code match; `--replace-wgsl <label>=<file>` matches by label and
is only right when one module carries the label). Tint mangles function names but keeps parameter names, so cut by
signature (`N, L, V, A` = `light_compute`; `idx` + `omni_lights` = `light_process_omni`; `shadow : texture_depth_2d`
returning f32 = a shadow sampler). `--log-pipelines <label substring>` prints the override constants each pipeline is
created with; `--f16` requests `shader-f16` so a substituted module may `enable f16;`.
`forward_clustered_explicit_fp16_bridge.patch` makes the Forward+ colour pass compile with `EXPLICIT_FP16` (null
result on a Mali-G615, see TASKS.md §2.9).
