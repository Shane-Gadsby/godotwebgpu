# Startup phase profiler (Task 14 subtask 1)

Brackets every phase of a WebGPU web export's startup and reports how much of the
wall clock each one owns, so that work on *reducing* the load stall is aimed at a
measured culprit rather than a guessed one.

```bash
node profile_phases.mjs --dir <export-dir> [options]
```

| option | meaning |
|---|---|
| `--dir <path>` | directory holding `index.html` (default: `../scene_smoketest/exports/demo_3d_platformer`) |
| `--duration <s>` | how long to observe after navigation (default 45) |
| `--warm` | reuse the browser profile, so HTTP and GPU shader caches are warm. Default is cold: fresh profile, GPU shader disk cache disabled |
| `--args <a,b>` | inject engine command-line args by rewriting `GODOT_CONFIG`'s `"args"` in the served HTML — e.g. `--args --verbose` profiles an existing export with verbose logging without re-exporting it |
| `--no-console` | detach the CDP console listener, to separate the page's own logging cost from the profiler's observer effect |
| `--eager-pipelines` | also create every pipeline through `create*PipelineAsync`, to test whether deferred GPU-side compilation is what a later blocking queue call waits on (it is not — Task 14 subtask 2, Measured 3) |
| `--headless` | default is headed, which WebGPU is generally happier with |
| `--label` / `--output` | name the run and its JSON |
| `--throttle <profile>` | cap the page's bandwidth through CDP: `off` (default), `fibre` (100 Mbit, 10ms), `cable` (20 Mbit, 30ms), `dsl` (5 Mbit, 50ms), `3g` (1.6 Mbit, 150ms). Chromium only |
| `--compress` | serve `.wasm`/`.pck`/`.js` with `Content-Encoding: gzip`, as a real static host does |

`--throttle` and `--compress` exist because every number this tool produced
before them was taken over loopback with no compression, which is nobody's
deployment. On loopback a 40 MB download costs ~130 ms, so download effects are
invisible; compressed and throttled they are often the largest term in the whole
load. They are also what settled §2.3: see `webgpu_notes/TASKS.md` §4.15 for the
figures and what they ruled out.

Nothing here needs an engine rebuild: all instrumentation is monkey-patched into
the page by `instrument.js` via `addInitScript`, so it works against exports
produced by any build of this fork. That is deliberate — it makes measuring the
cheap step and rebuilding the expensive one, which is the opposite of how this
task was approached before.

## The GPU process: `gpu_process_trace.mjs`

Everything below measures the page's main thread. The work behind a WebGPU call -- parsing a shader module,
compiling a pipeline -- runs on the GPU process's main thread (`CrGpuMain`), which also produces the page's
frames, and the startup advances one step per animation frame. So GPU-process work can stall the load without a
single slow call on the page. `gpu_process_trace.mjs` traces both processes with Dawn's trace categories from
navigation to the first frame and prints the page thread's busy/idle split (and V8's lazy wasm compilation), the
idle gap before each startup step, and what `CrGpuMain` was doing:

```bash
CI=1 node gpu_process_trace.mjs --dir <export-dir> [--args --verbose] [--warm] [--after <ms>] [--output trace.json]
```

`--after <ms>` keeps tracing that long past the first frame (default 500) and then lists every `CrGpuMain` task of
30 ms or more after it, with the Dawn calls inside, and which thread did each kind of pipeline work -- the frames that
stall once the game is running, not during the load. Without `CI=1` it runs system Chrome on the real GPU. It is
what showed that `createRenderPipelineAsync()` compiles on `ThreadPoolForegroundWorker` while `createRenderPipeline()`
compiles on `CrGpuMain` (TASKS.md §2.4, render pipelines compiled asynchronously).

It is what found §2.4's root cause: on the platformer, 89 compute pipelines (~5.9 s on swiftshader) and ~500
shader modules compiled on `CrGpuMain` before the first frame while the page sat idle between steps -- none of the
pipelines and ~5% of the modules were used. `CHROMIUM_PATH` overrides the browser binary.

## What it can and cannot see

On WebGPU the engine's first frame runs **synchronously inside `callMain()`**
(Task 14), so during the stall JS can observe nothing except the WebGPU calls the
engine itself makes. The report therefore splits the stall into:

- **WebGPU API total** — summed duration of every `createShaderModule`,
  `create*Pipeline`, `createTexture`/`createBuffer`, `queue.writeBuffer`/
  `writeTexture`/`submit` call made inside the window.
- **NOT in a WebGPU call** — the remainder, by subtraction. This is engine CPU
  work: resource decode, scene parse, GDScript, engine/rendering init.

The stall's length is also reported as the longest `requestAnimationFrame` gap,
which is measured without relying on the engine's own events at all, as an
independent check on the first number.

Breaking the CPU remainder down further needs marks emitted from C++ inside
`callMain()`; this tool cannot do it from outside.

## Reading the output

Per-call detail lands in the JSON (`phases_<label>.json`): every shader module,
pipeline, queue write (with its destination texture's label/format/size) and
console line, each with a timestamp relative to navigation. A useful habit is to
sort the queue writes by duration — one write per load blocks for ~1s while the
other ~1400 cost microseconds, and it is a wire flush, not an upload.
