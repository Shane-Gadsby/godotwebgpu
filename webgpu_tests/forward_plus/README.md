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
WEBGPU_REAL_GPU=1 node run_forward_plus.mjs --parity    # ... and every frame against native Vulkan
node run_forward_plus.mjs --list                        # what is and is not covered
WEBGPU_REAL_GPU=1 node run_forward_plus.mjs --feature ssao   # one feature
WEBGPU_REAL_GPU=1 node run_forward_plus.mjs --feature ssao --console   # ...and print the page's console (add "--verbose" to GODOT_CONFIG args in export/index.html for driver detail)
```

## Parity against native Vulkan

The delta test cannot tell a correct effect from a wrong one of a similar
size. `--parity` closes that gap: the harness renders the fixture natively with
Vulkan (the editor binary from `GODOT_EDITOR_BIN`, `--godot`, or the repo's
`bin/godot.linuxbsd.editor.x86_64`; `--reference DIR` reuses an earlier
render), then fails every feature whose WebGPU frame differs from that render
by more than its tolerance — `PARITY_TOLERANCE` (0.05 mean absolute difference)
unless the feature's `parityTol` says otherwise, with the reason written there.
Identical work measures 0.002–0.006 between the two backends; every bug this
mode was built to catch measured 0.06–25.

Its first run found, in features the delta test passed: `DEPTH_TEXTURE` reading
zeros (refraction and proximity fade with it), volumetric fog without shadows,
SDFGI corrupted by camera movement, contact shadows smeared along the screen
edge, every cube omni shadow missing, and the samplers' mip LOD bias ignored.
See `webgpu_notes/TASKS.md` §4.29.

Getting a comparable frame out of both backends took four things, all part of
the mode now, and each was a false failure before it existed:

- **A fixed timestep** (`--fixed-fps 60` on both sides; the harness adds it to
  the served `index.html`). Without it, particles simulated a different amount
  of time on each side.
- **One long settle** (`--settle`, default 240 frames; `--fp-settle=N`
  natively, the hash's third field on the web). Native Vulkan refilters a
  changed sky's radiance over several frames, and at the matrix's default 4
  frames height fog looked "wrong" — on the native side.
- **A frozen frame.** The fixture stops particles and pauses the render loop
  on the frame it reports `READY` for, because the harness's screenshot is
  asynchronous and otherwise lands some frames later — enough to move every
  particle and every TAA/FSR2 jitter sample.
- **A fixed jitter phase.** Volumetric fog's Halton offset is the frame number
  mod 16, and a settled temporal effect still differs by phase: natively, two
  adjacent phases measure 0.07 apart. The fixture delays each config until its
  capture frame lands on phase 0 mod 32 (`_aligned()`; `--fp-phase=N` natively
  for triage) and prints `[FP] FRAME <n> (phase <p>)` before `READY`. 32, not
  16, because FSR2 at the matrix's 0.5 scale cycles 32 jitter phases. Before
  that, the phase depended on whether the harness's first hash change landed in
  boot frame 1 or 2, and baked exports, which boot fast enough to straddle it,
  measured fog at 0.046 or 0.082 from native at random.

Arms marked `parityOnly` in `features.mjs` exist for this mode alone: their
correct "on" frame equals the "off" frame (`sdfgi_camera_move` flies the camera
away and back; SDFGI must end up exactly where it started).

Occlusion culling's correct "on" frame also equals its "off" frame, but there
the point is what was *not* drawn. Its arm sets `maxDelta` (the frame must not
change) and `infoDrop: { objects: 15 }`: the fixture prints `[FP] INFO
objects=.. primitives=.. draws=..` (`Viewport.get_render_info`) for every
captured frame, writes the same to `<id>_<0|1>.info` natively, and the harness
requires "on" to draw at least 15 fewer objects than "off", on the web and, in
parity mode, natively.

The four NaN arms (`dof_nan`, `glow_nan`, `ssr_nan`, `auto_exposure_nan`,
2026-10-26, `webgpu_notes/TASKS.md` §4.41, §4.44) are `maxDelta` arms too:
"on" adds a 2x2 px quad that writes NaN with the effect running both ways, and
the frame may change by at most 0.05, because the effect must keep the NaN to
the quad's own pixels rather than spread it into a bokeh shape, a glow blob, a
smeared reflection or a blank frame. Their GDScript-built materials can never
be baked, which is part of why the baked run's translated-stage ceiling is 150
(`webgpu_notes/TASKS.md` §2.39).

The ten camera arms (category `camera`, 2026-10-28, work task 44) cover this
fork's deterministic CameraAttributes and Environment camera effects: lens
optics, aperture/cat's eye bokeh, longitudinal chromatic aberration, tilt-shift,
starburst, halation, lens flare, veiling glare with the diffusion filter, the
camera LUT and in-camera noise reduction with sharpening. Grain, animated
zebras and the motion-dependent effects (motion blur, rolling shutter, shake)
change every frame and are left out. The glare arms show a small, very bright
sphere (`glare_sphere`); `longitudinal_ca` and `tilt_shift` switch the camera to
`cam_attrs_physical` (a 23 mm lens framed like the default camera, f/1.4,
focused at 4 m), and `_reset()` switches it back. The LUT is a 17^3
`ImageTexture3D` built in `_build_camera_effects()`.

An arm can carry `knownFail: '<why>'`: a web failure that is understood and out
of this port's reach. It runs, reports `KNOWN` and does not fail the run; if
it starts passing it reports `XPASS` and fails, so the marker goes with the
bug. No arm carries one today.

`needsRaycast: '<why>'` marks an arm that needs the raycast module (Embree, the
only occlusion culler): `occlusion_culling`. Stock Godot leaves the module out of
web templates (upstream GH-70621); this fork's include it by default since
2026-10-26 (`platform/web/detect.py`, `build-linux.sh --no-web-raycast` to leave
it out). The harness looks for Embree in the export's `index.wasm`
and prints `template raycast module (occlusion culling): yes|no`; without it
the arm becomes a `knownFail` with that reason (the frame is right, nothing is
culled), with it a normal arm that must cull as natively (29 -> 8 objects).

Natively, `--fp-only=a,b` limits the reference sweep, for triage:

```bash
godot --path godot/fp_check --rendering-driver vulkan --resolution 640x360 --fixed-fps 60 \
      -- --fp-matrix=/tmp/fpref --fp-settle=240 --fp-only=volumetric_fog,fsr2
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
It holds only what WebGPU cannot have: multiview, VRS, and MSAA 2x/8x (the
spec allows 1 and 4 samples).

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
location.hash = "#fp=<id>:<0|1>[:<settle>]"   →   [FP] READY <id> <0|1>
```

`_reset()` returns every knob to baseline before each config, so results do not
depend on matrix order: the fixture's resources (environment, camera
attributes, the probe material, the particles' process material) are restored
from a snapshot of every stored property taken at build time, and the nodes
from an explicit list. The list alone used to miss knobs — `alpha_scissor`'s
gradient albedo stayed on the probe for every later test. Features whose result accumulates (SDFGI, volumetric
fog, TAA, auto-exposure, particles) declare extra settle frames; particles are
pinned with a fixed seed and fixed step so their A/B is reproducible.

Everything is built procedurally in GDScript rather than committed as a `.tscn`
with binary resources — a skinned mesh, blend shapes, a MultiMesh and a
generated LOD chain cannot be expressed in text otherwise, and binary fixtures
rot silently across upstream syncs. The exceptions are what a shipped project
gets from the editor rather than at run time: the LOD mesh (`make_lod_mesh.gd`),
VoxelGI's data (`make_bake_assets.gd`) and the lightmap. `lightmap_room.tscn` is
a small room parked at x = -60, baked by `./bake_lightmap.sh`: `LightmapGI.bake()`
is not exposed to scripts and the lightmapper needs a GPU, so the script runs
the editor under Xvfb with Vulkan on a scratch copy of the project, with a
throwaway plugin that presses the 3D toolbar's own "Bake Lightmaps" button,
and copies back `lightmap_room.{tscn,lmbake,exr,exr.import}` (about 150 KB).
Re-run it after changing the room. The compositor arm's effect lives in
`fp_compositor_effect.gd`.

If a feature fails *with driver errors*, the harness reloads the page before
continuing: a broken texture leaves its uniform sets invalid and keeps erroring
every frame afterwards, and those errors otherwise land during the next
feature's window and get blamed on it. That is not hypothetical — VoxelGI's
failure was initially reported against `proximity_fade`, which is innocent.
