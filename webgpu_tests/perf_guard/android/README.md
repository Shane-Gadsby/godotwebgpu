# Profiling a WebGPU export on an Android phone (Chrome) over USB

What a Godot WebGPU export actually does on a real phone: fps and frame-time percentiles per export variant, GPU
time per pass from timestamp queries, and the WGSL dump/substitute loop for bisecting a slow shader on the device.
First used 2026-10-17 on a Motorola Razr 60 (MediaTek MT6878, Mali-G615 MC2, Android 16, Chrome 154); the results
and the conclusions drawn from them are in `webgpu_notes/TASKS.md` §2.9 (and summarised in
`webgpu_site/PERFORMANCE_AND_OPTIMIZATION.md`, "The First Phone").

## Requirements

- Node 22+ (global `WebSocket` and `fetch`; nothing to `npm install`).
- `adb` with the phone authorised for USB debugging (`adb devices` must say `device`, not `unauthorized`).
- Chrome on the phone. WebGPU is on by default in current Chrome for Android; no flags.
- A web export to profile. Export it with **`variant/separate_render_thread=false`** (see *Limits* below).

## Setup

```bash
adb reverse tcp:8099 tcp:8099
```

The scripts serve the export on `127.0.0.1:8099` with COOP/COEP headers; through the reverse tunnel the phone loads
`http://localhost:8099/`, which is a secure context, so WebGPU works without HTTPS or certificates. The scripts also
run `adb forward tcp:9222 localabstract:chrome_devtools_remote` themselves (and re-run it after every Chrome restart,
because the abstract socket dies with the process) and talk raw CDP over it — `cdp.mjs` is a 90-line client; Playwright
is not involved.

Every measuring script does the same preamble: force-stop Chrome, open a blank page at `/__blank`, **close every
other tab** (Chrome restores old tabs on relaunch; a second engine tab shares the GPU and the first number taken this
way was wrong), attach, install instrumentation with `Page.addScriptToEvaluateOnNewDocument`, then navigate to
`index.html`. "Cold" therefore means a fresh Chrome process, not a cleared shader cache.

## Tools

```bash
cd webgpu_tests/perf_guard/android

# Adapter info and limits, which renderer the engine picked (Forward+ vs Mobile), fps, filtered console.
node probe.mjs --dir <export> [--settle 35000] [--measure 8000]

# fps / p50 p90 p99 frame ms / main-thread busy % / WebGPU calls per frame, per export, cold, one after another.
# Boot is detected by the engine's first GPUQueue.submit on the page.
node fps_matrix.mjs --root <dir containing one subdir per export> --variants a,b,c [--settle 25000] [--measure 10000]

# GPU time from timestamp queries: per command buffer, or per pass (--passes) ranked by ms/frame and named by
# pipeline label and first attachment. Reuses the desktop perf_guard/gpu_time.mjs instrumentation verbatim.
node gpu_time.mjs --dir <export> [--passes [--top 40]] [--settle 30000]

# Shader bisection on the device (see below).
node gpu_time.mjs --dir <export> --dump-wgsl <label substring> --dump-dir D --dump-only
node gpu_time.mjs --dir <export> --passes --replace-pair D/<orig>.wgsl=<edited>.wgsl [--replace-pair ...]
node gpu_time.mjs --dir <export> --log-pipelines <label substring> ...     # override constants per pipeline
node gpu_time.mjs --dir <export> --f16 ...                                 # request shader-f16 on the device

# Helpers.
python3 -I wasm_memory_kind.py <file.wasm>...      # shared (pthreads) vs own memory: which template an export really is
python3 -I pck_diff.py a/index.pck b/index.pck      # what differs between two exports' pcks (textures, shader cache)
```

Reading `fps_matrix.mjs` rows: the frame times are quantised to the panel's refresh ticks (8.3 ms at 120 Hz), so a
GPU-bound scene reads as 41.5/83 (5 and 10 ticks) rather than a smooth distribution; `main busy %` is
`Performance.getMetrics` task time, which under main-thread rendering includes the engine; `gpucalls/f` counts every
`GPU*` method call on the page.

## Bisecting a slow shader on the device

1. Find the pass with `--passes`; its row names the pipeline, e.g. `SceneForwardClusteredShaderRD:18` (for the
   Forward+ scene shader, indices 0-17 are the depth variants ×{normal, ubershader}; the colour pass starts at 18,
   so `:18` is the plain specialized colour pass).
2. Dump the modules: `--dump-wgsl 'mod:SceneForwardClusteredShaderRD:18' --dump-only`. Module labels are
   `mod:<shader>:<variant>:stg<stage>` (fragment = `stg1`); **one label covers every material version** of the
   shader, so the dump writes every distinct module as `#0`, `#1`, ... (lit materials are ~180 KB, unshaded ones
   ~25 KB).
3. Edit copies. Tint mangles function names (`v_291`) but keeps parameter names, so cut by signature:
   `N, L, V, A, light_color, ...` is `light_compute`; `idx : ptr<function, u32>` plus `omni_lights` /
   `spot_lights` / `area_lights` in the body is `light_process_omni/spot/area`; `ref_index` is
   `reflection_process`; a function returning `f32` whose first parameter is `shadow : texture_depth_2d` is a
   shadow sampler (called from the omni/spot functions = positional, from the fragment body = directional); the
   function `main_inner` calls is the fragment body. An early `return;` as the first statement neutralises a
   function (the header turns the unreachable-code diagnostic off); `textureSampleCompare(...)` → `1.0f` removes
   shadow taps (match parentheses).
4. Substitute with `--replace-pair <orig>=<edited>` per module, exact code match, and re-run `--passes`. Always run
   the unedited modules through the same path first as a control (`--replace-pair orig=orig`): replacing one
   material's module into every module of the label breaks pipeline creation and the pass silently vanishes.
   `--replace-wgsl <label>=<file>` (label match) is only right when a single module carries the label.

`forward_clustered_explicit_fp16_bridge.patch` makes the Forward+ colour pass compile with `EXPLICIT_FP16` (Forward
Mobile's lighting include takes `half`; this bridges Clustered's float locals at the call boundary). On the Mali-G615
it was a null result (47.9 vs 48.7 ms); it is kept for Apple GPUs.

## Limits

- **Separate render thread**: a thread-enabled export with `variant/separate_render_thread=true` makes all its
  WebGPU calls in a pthread worker. The page then shows ~12% main-thread busy and *zero* GPU calls — that is the
  worker rendering, not an idle page — and `Emulation.setDeviceMetricsOverride` cannot resize its OffscreenCanvas.
  The worker never returns to its event loop long enough to answer `Runtime.evaluate` (idle pool workers sit in
  `Atomics.wait`), so neither can the instrumentation be driven from outside. `worker_gpu.mjs` injects the
  instrumentation before the workers run (`Target.setAutoAttach` with `waitForDebuggerOnStart`) and tries to make it
  self-driving and report over the console; it is unfinished. Export with `separate_render_thread=false` to measure
  — on the Razr it makes no fps difference.
- Screenshots on a foldable: `adb exec-out screencap -d <display id> -p` (`dumpsys SurfaceFlinger --display-id`);
  without `-d` the output is a text warning, not a PNG.
- Unbaked exports translate shaders at runtime; give them enough settle (the Forward Mobile export needed more than
  25 s, and its first matrix row was wrong because of it). Check `shader sources: baked N, translated M` in the
  `gpu_time.mjs` output.
- Everything here assumes Chrome's devtools socket; Firefox for Android has no WebGPU yet.
