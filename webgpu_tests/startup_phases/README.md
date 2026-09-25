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
| `--headless` | default is headed, which WebGPU is generally happier with |
| `--label` / `--output` | name the run and its JSON |

Nothing here needs an engine rebuild: all instrumentation is monkey-patched into
the page by `instrument.js` via `addInitScript`, so it works against exports
produced by any build of this fork. That is deliberate — it makes measuring the
cheap step and rebuilding the expensive one, which is the opposite of how this
task was approached before.

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
