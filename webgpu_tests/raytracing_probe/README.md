# Software ray tracing probe

How many rays per second a WGSL BVH traversal reaches on an adapter, through the browser — and a correctness
test of that traversal. WebGPU has no hardware ray tracing and none is scheduled (gpuweb issue #535 sits at
"Milestone 4+"), so any ray or path tracing in the WebGPU Forward+ renderer is compute-shader traversal of an
acceleration structure we build ourselves, in storage buffers. This probe is that kernel on its own, with
nothing of the engine around it, so the number it prints is the ceiling the renderer work has to fit under.
The investigation it feeds is `webgpu_notes/TASKS.md` §2.34; the pick-up instructions are in §1
"Experiments in flight".

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

## What it checks

The run is a test, not only a benchmark. For every scene:

- **Traversal against brute force.** A grid of `verify` (default 64, rounded to the aspect ratio) primary
  rays is re-traced on the CPU over every triangle in double precision with the same Möller–Trumbore test;
  the GPU's hit distance must match within `2e-4 × scene half-width + 1e-4 × t`, and miss must match miss.
  One mismatch per 50 rays is tolerated for edge-grazing rays that land differently in f32 and f64.
- **Plausibility for the fixed camera**: primary hit fraction 45–80 % (it is ~61 %), shadow occlusion
  5–60 % (20–30 %), bounce hit fraction 5–70 % (20–35 %), mean traversal steps under 80.
- **BVH depth** fits the 48-entry per-thread stack (the build caps depth at 44).
- No uncaptured WebGPU errors, and every requested scene completed.

Exit codes: **0** every check passed, **1** a check failed or the probe errored, **2** a real-GPU run landed
on the fallback adapter (the numbers are then swiftshader's). Checks are listed in the result JSON under
`checks`, failures are printed by the runner.

## Running

```bash
cd webgpu_tests/raytracing_probe
node run_probe.mjs                              # real GPU: system Chrome, headed, 1080p, 100k and 1M triangles
node run_probe.mjs --tris 500000 --iters 50     # one scene, more samples
node run_probe.mjs --shot probe.png             # keep a picture of the last scene
node run_probe.mjs --software                   # swiftshader, 320x180, 20k triangles: correctness only, exit 0/1
node run_probe.mjs --url-only --port 8099       # just serve; open the URL in a browser you already have up
```

Flags: `--tris a,b,…` triangle targets, `--w`/`--h` resolution, `--iters` timed dispatches per kernel,
`--verify N` brute-force sample count (0 disables), `--port` a fixed port, `--keep-open` leaves the browser up.

The real-GPU launch is the one every real-GPU tier uses (system Chrome, `--enable-unsafe-webgpu
--enable-features=Vulkan --use-vulkan --ignore-gpu-blocklist`, `headless: false`). **Check `nvidia-smi`
first**: another process using the GPU (a game, a loaded local LLM) makes the numbers meaningless — that is
why the 2026-10-23 session has only software-adapter numbers. Results are written to `results/latest.json`
and a timestamped copy beside it (`results/` is ignored by git); the numbers that matter are recorded in
`webgpu_notes/TASKS.md` §2.34.

**On the phone** (Motorola Razr 60 over USB): `node run_probe_phone.mjs [--shot phone.png --display
4627039422300187648]`. It serves the probe on port 8099, runs `adb reverse` so `http://localhost:8099/` is a
secure context on the device, starts Chrome, opens the tab through CDP (`Target.createTarget` on the browser
socket — a VIEW intent does not reliably open a tab on a locked phone, and Chrome 155 refuses `/json/new`),
polls the page's log, reads `window.__probeResult`, takes the screenshot, and closes only the tab it opened.
Defaults there are 1280×720, 100 k triangles, 10 dispatches, 32 verified rays. The manual route still
works: `node run_probe.mjs --url-only --port 8099`, `adb reverse tcp:8099 tcp:8099`, open the printed URL in
the device's Chrome; the result is on the page.

Playwright is imported from `../scene_smoketest/node_modules`, so that tier's `npm install` is the only
prerequisite.

## Reading the numbers

At 1920x1080 a kernel traces about 2.07 M rays (fewer for `shadow` and `bounce`, which skip pixels the
camera ray missed). A 60 fps frame has ~16 ms in total, and a hybrid effect can reasonably take 3-5 ms of
it, so a full-resolution 1-ray-per-pixel effect needs **400-700 Mrays/s** for its ray type, and a
half-resolution one with temporal accumulation needs a quarter of that. The `1 spp` line adds the three
kernels: what one sample per pixel of primary + shadow + bounce costs, the floor for any path-traced
mode. The decision gate written in §2.34: 300 Mrays/s on `bounce` on the desktop makes RT AO and shadows
viable at half resolution, 600 at full resolution, and below about 100 only an offline reference tracer is
worth building.

Software-adapter numbers (swiftshader) are a correctness check only; they are two to three orders of
magnitude below real hardware.

Measured 2026-10-23 (details and the decision in `webgpu_notes/TASKS.md` §2.34): RTX 4080 SUPER at 1080p,
primary 5.3 / 3.7 Grays/s, shadow 5.4 / 3.8, incoherent bounce 2.3 / 1.9 (93 k / 945 k triangles), 1 spp
1.2–1.6 ms; Razr 60 (Mali-G615) at 720p, primary 45 Mrays/s, shadow 40, bounce 19, 1 spp 65 ms. Desktop passes
the gate several times over; the phone is a hundred times slower, so ray tracing is a desktop-class feature.
