# Forward+ feature matrix (WebGPU)

Proves that every Forward+ feature still *does something* on the WebGPU port.

For each feature the fixture renders the same scene twice — once with the
feature off, once on — and the harness asserts the frame actually changed by at
least a calibrated amount, with no driver errors in either render.

That is a weak assertion about correctness and a strong one about existence,
deliberately. The failure mode this port keeps producing is a feature **silently
doing nothing**: a shader that fails to convert, a texture that fails to
allocate, an effect skipped behind a capability check. None of those log an
error, and none of them look wrong unless you compare against the same frame
without the effect. SSAO and SDFGI both shipped broken through a completely
green test suite in the 4.8 port (`webgpu_notes/TASKS.md` Task 15.5) for exactly
this reason.

```bash
./export.sh                                             # after an engine build
WEBGPU_REAL_GPU=1 node run_forward_plus.mjs             # the matrix
node run_forward_plus.mjs --list                        # what is and is not covered
WEBGPU_REAL_GPU=1 node run_forward_plus.mjs --feature ssao   # one feature
```

## This tier requires a real GPU

**Not a preference — a correctness requirement.** Under a software adapter the
results are not a representation of what the port does:

- A software adapter reports fewer than 48 textures per shader stage, and
  `RendererCompositorRD::initialize()` then silently falls back to **Forward
  Mobile**. Roughly a third of this matrix does not exist there: SSAO and SSIL
  are Forward+-only, SDFGI's voxelization lives in the clustered shader, and
  `render_forward_mobile.cpp` contains no volumetric fog code at all.
- Even when the banner says Forward+, measured under `--use-angle=swiftshader`
  several effects (volumetric fog among them) render nothing at all.

So the harness **skips rather than passes** when it cannot get a real adapter,
exiting with code 2, which `local_ci.sh` reports as SKIP. A tier that passes by
not running is worse than no tier. Specifically:

| environment | behavior |
|---|---|
| Chrome with `WEBGPU_REAL_GPU=1` | runs |
| Chrome without it | SKIP — the default launch forces swiftshader |
| Firefox with a display | runs |
| Firefox headless (`CI=1`) | SKIP — cannot composite here, canvas reads back black |
| any adapter resolving to Forward Mobile | SKIP — detected from the startup banner |

There is no software renderer available here that is a faithful stand-in. If
one appears, it has to clear the same bar: report Forward+ *and* actually render
the Forward+-only effects. Check both before trusting it.

## Rule: a new feature needs a test

**Any feature added to the renderer — including code merged from upstream Godot
— must get an entry in this matrix, or an entry in the `UNCOVERED` list in
`features.mjs` saying why not.**

This is enforced, not merely requested. The fixture publishes its own feature
list at boot (`[FP] FEATURES ...`) and the harness asserts it matches
`features.mjs` exactly, in both directions:

- an `_apply()` arm with no `features.mjs` entry → `DRIFT ... add one`
- a `features.mjs` entry the fixture cannot drive → `DRIFT ... add an _apply() arm`

Either fails the run and names the offender. Adding a feature therefore takes
two edits that cannot drift apart:

1. **`godot/fp_check/fp_scene.gd`** — add the id to `FEATURES`, add an `_apply()`
   arm that turns the feature on when `on` is true and leaves it off otherwise,
   and add a `SETTLE_OVERRIDE` entry if the effect accumulates over frames.
   Add whatever scene content it needs to `_build_*()`.
2. **`features.mjs`** — add `{ id, cat, label, minDelta }`.

`UNCOVERED` is the honest escape hatch, not a dumping ground: each entry must
say *why*, and anything whose reason stops being true belongs in the matrix.
It currently holds LightmapGI (needs editor-baked data), occlusion culling (a
correct result is pixel-identical, so an A/B delta cannot see it — it needs a
draw-call assertion instead), multiview and VRS (absent from WebGPU by design),
CompositorEffect (no engine behavior to assert), and three that genuinely
should be added: FogVolume nodes, stencil, and bent normals.

### On upstream syncs

Run this matrix before and after a sync. Features that stop registering are the
port breaking against upstream's changes, and the per-feature delta is a far
better signal than "the scene still loads". The 4.8 port would have caught both
of its black-screen regressions here.

## Calibrating thresholds

`minDelta` is a floor on mean absolute pixel difference (0–255), set at roughly
a third of the delta measured on **native Vulkan**, not on WebGPU. That matters:
calibrating against WebGPU would bake in whatever the port currently does,
including a bug.

The fixture can render the whole matrix through any native backend:

```bash
godot --path godot/fp_check --rendering-driver vulkan --resolution 640x360 \
      -- --fp-matrix=/tmp/fpref
```

This writes `<id>_0.png` / `<id>_1.png` per feature. Comparing those deltas
against the WebGPU run is also the triage tool when a feature fails, and it
separates the two cases cleanly:

| Vulkan | WebGPU | meaning |
|---|---|---|
| large | large | works; threshold is too high |
| ~0 | ~0 | the **fixture** does not exercise the feature — fix the scene, do not lower the threshold |
| large | ~0 | a **port bug** |

That last column is what this suite is for. Its first run found one: VoxelGI
failed with `Format 'R8G8_Uint' does not support usage as storage image`,
because the driver's storage-format capability switch omitted the 8-bit integer
formats although `_promote_storage_format()` had always handled them — the same
omission class as a previously fixed `R16_UINT` bug.

**Do not fix a failure by lowering its threshold** unless the Vulkan column says
the feature genuinely has that little effect. Lowering a threshold to get green
converts this suite back into the thing it exists to replace.

## How it works

One export, many configurations. The fixture polls `location.hash` and
reconfigures live, so the matrix costs one engine boot rather than 130:

```
location.hash = "#fp=<id>:<0|1>"   →   [FP] READY <id> <0|1>
```

`_reset()` returns every knob to baseline before each config, so results do not
depend on matrix order. Features whose result accumulates (SDFGI, volumetric
fog, TAA, auto-exposure, particles) declare extra settle frames; particles are
pinned with a fixed seed and fixed step so their A/B is reproducible.

Everything is built procedurally in GDScript rather than committed as a `.tscn`
with binary resources — a skinned mesh, blend shapes, a MultiMesh and a
generated LOD chain cannot be expressed in text otherwise, and binary fixtures
rot silently across upstream syncs.

If a feature fails *with driver errors*, the harness reloads the page before
continuing: a broken texture leaves its uniform sets invalid and keeps erroring
every frame afterwards, and those errors otherwise land during the next
feature's window and get blamed on it. That is not hypothetical — VoxelGI's
failure was initially reported against `proximity_fade`, which is innocent.
