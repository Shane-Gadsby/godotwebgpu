# Software ray tracing probe

How many rays per second a WGSL BVH traversal reaches on an adapter, through the browser. WebGPU has no
hardware ray tracing and none is scheduled (gpuweb issue #535 sits at "Milestone 4+"), so any ray or path
tracing in the WebGPU Forward+ renderer is compute-shader traversal of an acceleration structure we build
ourselves, in storage buffers. This probe is that kernel on its own, with nothing of the engine around it,
so the number it prints is the ceiling the renderer work has to fit under. The investigation it feeds is
`webgpu_notes/TASKS.md` §2.34.

## What it measures

A deterministic procedural scene (a subdivided ground plane and a grid of tessellated spheres and boxes,
sized to a requested triangle count), a binned-SAH BVH2 built in JavaScript (leaves of up to 4 triangles,
children stored contiguously, 32-byte nodes, 48-byte triangles as `v0, edge1, edge2`), and one compute
shader with three entry points, each timed over `iters` dispatches with GPU timestamp queries (median), or
wall clock where `timestamp-query` is missing:

| kernel | rays | what it stands for |
|---|---|---|
| `primary` | one coherent camera ray per pixel, closest hit | a full-screen trace; also produces the hit points the other two start from |
| `shadow` | one ray from each hit toward a point light, any hit | ray-traced shadows (the cheapest hybrid effect) |
| `bounce` | one cosine-weighted hemisphere ray from each hit, closest hit | incoherent GI / AO / reflection rays, the expensive case |

It also prints the adapter's features and the limits that bound a software ray tracer (storage buffer
binding size, buffers per stage, workgroup limits, subgroup sizes), the BVH build time (a proxy for a CPU
build in wasm), tree depth and the mean and maximum traversal steps per ray. The page draws the last
result — normals, darkened where the shadow ray was blocked — so a wrong traversal is visible at a glance.

## Running

```bash
cd webgpu_tests/raytracing_probe
node run_probe.mjs                              # real GPU: system Chrome, headed, 1080p, 100k and 1M triangles
node run_probe.mjs --tris 500000 --iters 50     # one scene, more samples
node run_probe.mjs --shot probe.png             # keep a picture of the last scene
node run_probe.mjs --software                   # swiftshader, 320x180, 20k triangles: checks the kernels run, not the hardware
node run_probe.mjs --url-only                   # just serve; open the URL in a browser you already have up
```

The real-GPU launch is the one every real-GPU tier uses (system Chrome, `--enable-unsafe-webgpu
--enable-features=Vulkan --use-vulkan --ignore-gpu-blocklist`, `headless: false`); the runner exits 2 if
the run landed on the fallback adapter. Check `nvidia-smi` first: another process using the GPU (a game,
a loaded local LLM) makes the numbers meaningless. Results are written to `results/latest.json` and a
timestamped copy beside it (`results/` is ignored by git); the numbers that matter are recorded in
`webgpu_notes/TASKS.md` §2.34.

Playwright is imported from `../scene_smoketest/node_modules`, so that tier's `npm install` is the only
prerequisite.

## Reading the numbers

At 1920x1080 a kernel traces about 2.07 M rays (fewer for `shadow` and `bounce`, which skip pixels the
camera ray missed). A 60 fps frame has ~16 ms in total, and a hybrid effect can reasonably take 3-5 ms of
it, so a full-resolution 1-ray-per-pixel effect needs **400-700 Mrays/s** for its ray type, and a
half-resolution one with temporal accumulation needs a quarter of that. The `1 spp` line adds the three
kernels: what one sample per pixel of primary + shadow + bounce costs, the floor for any path-traced
mode.

Software-adapter numbers (swiftshader) are a correctness check only; they are two to three orders of
magnitude below real hardware.
