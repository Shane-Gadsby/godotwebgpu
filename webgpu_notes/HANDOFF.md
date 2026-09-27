# Handoff — WebGPU work in progress

**As of 2026-09-27, with the scene tier fully green.** Branch `webgpu-4.7.2`. The engine work landed
in `5f4b63c136` (the `depth_buffer` reclassification), the depth-back-copy commit after it, and the
gradient-readback commit after that (Task 45); `bin/` was built at the last of those (§6).

This is a snapshot for picking the work up cold. `webgpu_notes/TASKS.md` remains the living
detail; this file says where things stand, what is verified, what is *mis*-recorded elsewhere, and
which traps cost time. Task numbers below index into TASKS.md.

**The 60-second version**: **every tier is green, with nothing skipped.** The scene smoketest is
**19 pass, 0 fail, 0 skip** in both Chrome and Firefox. The last skip, `demo_compute_heightmap`, is
fixed (Task 45, §4.6) and its `known_limitation` flag is gone from `scenes.json`, so there is no
longer any tracked known-broken scene. `bin/` matches `HEAD`'s engine work and the exports in `webgpu_tests/scene_smoketest/exports/` were made from it, so a
`node run_scenes.mjs --skip-export --browser chrome` reproduces that result in ~8 minutes with no
rebuild. §8 says what is worth doing next; nothing there is a known bug.

---

## 1. One-paragraph status

The web export's load time and its Firefox support both moved a long way (Tasks 14, 38, 41, 44), the
test suite now covers roughly twice what it did (Tasks 42, 44), and **Tasks 44 and 45 are closed**:
all five scene failures they exposed are fixed, and the tier is green in both browsers with nothing
skipped. Two of Task 44's were
independent bugs behind one scene feature — Godot's `DEPTH_TEXTURE` (proximity fade / refraction) both
tripped this driver's `depth_buffer` reclassification *and* triggered a depth back-copy that
dispatched an `rgba16f`-declared compute variant at an `R32_SFLOAT` destination (§4). Everything else
below is either landed and verified, or a correction to something previously written down wrongly.

---

## 2. Verified state, and when it was measured

| tier | result | measured |
|---|---|---|
| `shader_corpus` | **14/14** | 2026-09-27, after the reclassify fix |
| `driver_unit_tests` | **332/0** | 2026-09-27, after the reclassify fix |
| `preprocessing_tests` | **205/0, 1 skipped** | 2026-09-27, after the reclassify fix |
| Scene smoketest — Chrome | **19 pass, 0 fail, 0 skip** | 2026-09-27, after the Task 45 fix |
| Scene smoketest — Firefox | **19 pass, 0 fail, 0 skip** | 2026-09-27, after the Task 45 fix |
| `resource_lifecycle` | all pass | 2026-09-27 |
| `screenshot_comparison` | **8/0** (entry point is `screenshot_tests.mjs`) | 2026-09-27 |
| Native Vulkan spot check | `3d/particles`, 300 frames, clean | 2026-09-27, RTX 4080 SUPER |
| Scene smoketest — Safari | skipped | macOS only; never run here |

Both 19-scene runs were made against a freshly built editor + non-dlink nothreads template pair at
the current engine work, with a fresh export of every scene, and both exited 0. The native Vulkan spot
check is there because the last fix is in *shared* engine code (`renderer_scene_render_rd.cpp`), not
in `drivers/webgpu/` — anything under `servers/` needs one.

Nothing is skipped any more. `demo_compute_heightmap` passes (Task 45, §4.6) and its
`known_limitation` entry has been removed from `scenes.json`.

One harness trap, not caused by any fix: an export can die with
`ERROR: Parameter "singleton" is null.  at: is_cmdline_mode (editor_node.cpp:6622)` followed by
`Aborted`. That is a **stale `.godot` import cache** in the `godot-demo-projects` checkout, left by
an editor built at a different version hash — `rm -rf <project>/.godot` fixes it. It hit
`benchmark_sprites` and `demo_compute_heightmap` at the start of the Task 45 session and neither has
recurred since. Read it as a cache problem, not an engine crash. (The older note about
`benchmark_sprites` failing on a missing `res://benchmark_profiler.gd` was the same thing; it exports
cleanly now.)

**`./webgpu_tests/local_ci.sh --no-safari` passes end to end, and now actually tests the engine it
builds**: 2026-09-27, **15 passed, 0 failed, 1 skipped**, exit 0 — the skip is Safari, by the flag.
It covers two tiers missing from the table above, `spec_constant_overrides` and `wgsl_cache` (both
its Python and JS halves), and both are green.

**What it used to do, and why a green run meant less than it looked.** Stage 0 built the *dlink*
template, `…wasm32.nothreads.dlink.zip`, while the smoketest exports with the non-dlink
`…wasm32.nothreads.zip` — different filenames with different object directories, so they coexist
and neither clobbers the other. The dlink/non-dlink *mismatch* the earlier note feared therefore
could not happen, but the flip side was worse: **Stage 0 rebuilt a template no later stage loaded**,
and the smoketest stage did not re-export, so a full run tested whatever exports happened to be on
disk. A green result said nothing about the working tree's engine.

**Fixed.** Stage 0 now builds the **editor, the dlink template, and the non-dlink nothreads
template** — that last one, with the editor, being the exact pair the smoketest uses. It clears the
Task 40 stale `register_module_types.gen` objects per variant right before each web build, and
builds the non-dlink one **last** so it is the freshest thing on disk when the export runs. A new
step then re-exports all 19 scenes from those binaries before any browser runs. The dlink template
is kept purely as a compile check — nothing downstream loads it, but dropping it would let a
dlink-only build break through unnoticed; `--no-dlink` skips it.
Export failure is fatal rather than counted, because everything after it would be testing the
previous exports. `--no-export` keeps the old behavior, `--export` re-exports without rebuilding,
and export follows the rebuild by default. The script also sources `$EMSDK_ENV`
(default `~/emsdk/emsdk_env.sh`) when `emcc` is not already on `PATH`, which it previously required
the caller to have done; verified by running with `emcc` deliberately stripped from `PATH`.

`run_scenes.mjs` also recovers from the stale-`.godot` abort (§6) now: on that exact signature it
clears the cache and retries the export once. Rebuilding the editor is what arms that trap, and the
script rebuilds before every export, so it would otherwise have hit it routinely. Both directions
are tested with a stub editor — the signature retries and succeeds, an unrelated failure fails fast
without deleting anything.

---

## 3. What landed (all verified in a browser unless noted)

| # | Change | Effect |
|---|---|---|
| 14 | Startup phase profiler + `OS_Web` benchmark marks | Load time is now measurable; ~2.0s stall fully decomposed |
| 14 | Glyph-atlas upload coalescing (`frame_pre_draw`) | **~800 ms (41%) off** the user's project; 66→31 MB uploaded before first frame |
| 38 | `lower_subgroup_ops()` SPIR-V pass | **Firefox renders 3D again** (was: UI only, no scene at all) |
| 41 | tier2 storage-format promotion (`rgb10a2unorm` → `rgba16float`) | Firefox Octmap shaders run; **0 validation errors** where there were 14 |
| 44 | Canvas SDF `R16_SNORM` → `R16_SFLOAT` fallback | `demo_2d_particles` **735 errors → 0**, now passes |
| 44 | `depth_buffer` (group 1) excluded from `_reclassify_single_component_depth_textures()` | `demo_3d_platformer` + `stress_3d_platformer` **44 errors each → 0**; tier 15/3/1 → **17/1/1** in both browsers |
| 44 | Depth back-copy switched to `copy_depth_to_rect()` (`r32f` variant, not `rgba16f`) | `demo_3d_particles` **49 errors → 0**; tier → **18/0/1** in both browsers, exit 0 |
| — | Block-align compressed texture-to-texture copies | BPTC textures no longer render black |
| 45 | `Gradient{Texture1D,Texture2D}::get_image()` regenerate instead of reading the GPU back | `demo_compute_heightmap` **skip → pass**; tier → **19/0/0** in both browsers, nothing skipped |
| 43 | `create_local_rendering_device()` now reports why it failed | Was returning null silently |
| 42/44 | Smoketest: editor/template overrides, generated presets, forced WebGPU renderer, heightmap self-test | Demo tier runs at all, and runs *on WebGPU* |

Reference numbers worth keeping: the user's project loads with a **~1.0 s** cold stall (was ~9.2 s at
the start of this work), `{baked: 360, translated: 0}`, and BC1 texture compression is verified
equivalent to uncompressed at RMSE 0.0006.

---

## 4. Task 44, closed: two bugs behind one scene feature

### 4.1 The first bug: what it was
`demo_3d_particles`, `demo_3d_platformer`, `stress_3d_platformer` failed in Chrome and Firefox on:
```
None of the supported sample types (Float|UnfilterableFloat) of [Texture 1152x648 R32Float]
  match the expected sample types (Depth).
 - While validating entries[24] against { binding: 48, sampleType: Depth ... }
 - While validating [BindGroupDescriptor] against [BindGroupLayout "bgl:SceneForwardClusteredShaderRD:19:set1"]
```
Binding 48 = GLSL set 1 binding 24 (the driver doubles every non-combined binding) = `depth_buffer`,
declared `uniform texture2D` and fed `RB_TEX_BACK_DEPTH`: a *colour* `R32_SFLOAT` copy of depth,
polymorphic with `DEFAULT_RD_TEXTURE_DEPTH`.

### 4.2 The cause was ours, not Tint's
`_reclassify_single_component_depth_textures()` (`rendering_device_driver_webgpu.cpp:4372`, Task 7.13)
rewrites a `texture_2d<f32>` binding to `texture_depth_2d` when it is named "*depth*" and only ever
read one component at a time. Godot's `DEPTH_TEXTURE` (proximity fade, refraction —
`scene/resources/material.cpp:1805`/`:1834`, both `textureLod(depth_texture, …, 0.0).r`) is exactly
that shape, so the pass rewrote the scene shaders' `depth_buffer` and the BGL entry became
`sampleType: Depth` with a colour `R32Float` bound.

Three checks pin it, none of which needs a rebuild:
- 287 engine shaders dumped with `GODOT_DUMP_SPIRV` and converted with `tint_convert_cli` contain
  **zero** `texture_depth` outside the two genuine shadow atlases. Tint never promotes `depth_buffer`.
- Tint's `lower/texture.cc` is per-*variable* (`ConvertVarToDepth()` retypes one `var`;
  `ConvertTextureParam()` walks call sites and forks helpers), so the shared `OpTypeImage` is not a
  promotion vector at all.
- The WGSL captured from the running export is post-reclassification on its face:
  `textureSampleLevel(depth_buffer, …, 0i)` with **no `.x`** and an **`i32`** level is this pass's
  own rewrite; Tint emits `….x` with an `f32` level.

The "same BGL label built 12 times with different contents" needs no spec-constant story either:
every material shares one `ShaderRD`, so one label covers every material's variant 19.

### 4.3 The first fix
A fifth disqualifying signal in that pass, beside `half` and `dilated`: the exact name `depth_buffer`
in **group 1** (the scene shaders' render-buffers set, names fixed by engine GLSL). The WGSL then
keeps `texture_2d<f32>` and the `.x` swizzle, which is right for the `R32Float` copy. Group 1 gating
keeps `taa_resolve.glsl`'s and `cluster_debug.glsl`'s own `depth_buffer` (both set 0) rewriting as
before; a user material uniform of that name lands in group 3.

**Result**: `demo_3d_platformer` and `stress_3d_platformer` pass with gpu=0 (44 errors each before),
taking the tier from 15/3/1 to 17/1/1 in both browsers. `demo_3d_particles` needed §4.4 as well.

### 4.4 The second bug: the depth back-copy's storage format
`demo_3d_particles` kept failing after the fix above, on a genuinely separate cause:
```
Format (TextureFormat::R32Float) of [Texture (unlabeled 1152x648 px, R32Float)]
  expected to be (TextureFormat::RGBA16Float).
 - While validating entries[0] against { binding: 0, visibility: Compute,
     storageTexture: {format: RGBA16Float, viewDimension: e2D, access: WriteOnly} }
```
`_render_buffers_copy_depth_texture()` (`renderer_scene_render_rd.cpp:439`) copies depth into
`RB_TEX_BACK_DEPTH`, an `R32_SFLOAT` texture, through `copy_to_rect()` — whose compute variant
declares its storage image `rgba16f` (`copy.glsl:72`). WebGPU requires a storage binding's declared
format to match the bound texture's exactly, with none of Vulkan's compatibility-class laxity, so Dawn
rejected the bind group and every command buffer behind it (1490 cascade errors from one real one).
`copy_depth_to_rect()` is the same copy with the `r32f`-declared variant, and `ss_effects.cpp:1561`
already used it for this exact kind of copy. Identical in result on every backend — both copy the red
channel, and an `R32_SFLOAT` image stores nothing else.

**This is the third instance of one class**, after `copy.glsl`'s `DST_IMAGE_RG16F` (TAA's RG16F
velocity buffers). When a compute copy fails on WebGPU, the question is always: what format does the
destination texture really have, and what does the dispatched variant's `layout(...)` declare?
Upstream can leave those mismatched; we cannot.

**Both bugs sat behind one scene feature.** `DEPTH_TEXTURE` (proximity fade / refraction on the
particle materials) is what reclassified `depth_buffer` *and* what makes the back-depth copy run at
all. Fixing the first exposed the second.

### 4.5 Ruled out — do not re-investigate
- **Tint**, in every form: the SPIR-V type split, `fix_depth2_images()` (every image is `Depth=0`),
  and the `_depth_alias` clone mechanism (which does not exist in the vendored Tint —
  `wgsl_depth_alias_bindings` and its name-suffix scan are dead code and should be removed).
- **`split_depth_sampled_image_types()`**, the SPIR-V pass designed and reverted earlier: it was
  aimed at a producer that was never producing this. Do not revive it for this bug.
- **Changing `RB_TEX_BACK_DEPTH`'s format** — it is a colour attachment and storage image by design,
  and the binding is polymorphic anyway. TASKS.md Task 44 has the three independent reasons.
- **The BGL/driver scan** is not wrong; it faithfully reports what the WGSL says.
- **Everything in §4.4's earlier write-up as "suspects"** — `_promote_storage_format()` and the WGSL
  format remaps — turned out *not* to be involved: the layout's `rgba16float` came straight from the
  GLSL variant being dispatched, not from any remap. Checking which variant is bound before suspecting
  the remaps is the cheaper order.

## 4.6 Task 45, closed: the heightmap demo, and what `texture_2d_get()` can never do

`demo_compute_heightmap` was the last skip. The recorded diagnosis was wrong, and the way it was
wrong is the reusable part.

**What the notes said**: `GradientTexture1D.get_image()` returns empty, the self-test primes it over
120 frames and still gets nothing, so retrying is not the answer. **What the export actually did**:
the priming loop *succeeded* — its failure line never printed — and the first real error was the
demo's own `gradient_tex.get_image().get_data()`, one call later, at
`_texture_create (rendering_device.cpp:10309)`. Thirty seconds of `capture_errors.mjs --all` against
the already-committed export said so; no rebuild was needed to find it.

**Why priming was always going to fail.** `RenderingDeviceDriverWebGPU::texture_get_data()` is a
one-shot cache *on purpose*: a call either starts a readback (returns empty) or consumes a completed
one and clears `has_data`, with no auto-requeue. Calls therefore alternate, and the priming loop
consumed exactly the data it had waited for. **A caller that calls `texture_2d_get()` once can never
get data on WebGPU**, no matter how many frames anything waited first. That is a property of the
API's shape, not a bug to fix.

**The fix**: stop round-tripping. A gradient texture is a pure function of its `Gradient`, size and
`use_hdr`, so `_update()`'s generation half became `_generate_image()` and both classes'
`get_image()` call it directly. Identical by construction everywhere, cheaper everywhere, and it
works where the round-trip cannot.

**Deliberately not done**: populating `image_cache_2d` outside `TOOLS_ENABLED` to make
`texture_2d_get()` work in general. It would retain a CPU copy of every texture created from an
`Image` — on web that includes every `CompressedTexture2D` loaded from disk, i.e. roughly a doubling
of texture memory on the platform this fork exists to make fast. Not worth it for a rare API. If it
is ever revisited, it needs to be opt-in, not a default.

**Still true, and it is a platform limitation rather than a driver fault**: the demo's second
readback, `rd.texture_get_data()` on a local `RenderingDevice` after `rd.submit()/rd.sync()`, is
genuinely on the GPU and cannot be regenerated. `sync()` cannot wait for it (no Asyncify in the web
build), so the first call returns 0 bytes and the data lands a frame later. The smoketest patch
retries, which is the documented contract; the upstream demo calls once and shows an empty island on
WebGPU. Web GDScript that needs a readback must retry or use `texture_get_data_async()`.

---

## 4.7 GitHub Actions: red for two days on a werror nobody could see locally

`gh run list --repo Shane-Gadsby/godotwebgpu` showed 🧪 WebGPU Tests failing on every push since
2026-09-25, always in the same step (`Build WebGPU export template`). The logs had expired, but the
step name plus the timings said enough: successful runs take 47-49 minutes, the three failures died
at 6-9. Reproducing the job's exact scons line locally found it in one build:

```
drivers/webgpu/spirv_preprocess.cpp:3209:7: error: unused variable 'carries_literals'
  [-Werror,-Wunused-variable]
```

**Why nothing here caught it.** `webgpu_tests.yml` sets `SCONS_FLAGS: dev_mode=yes`, and in Godot
`dev_mode` implies `warnings=extra werror=yes`. No local build used it — not the commands in
CLAUDE.md, not `local_ci.sh` — so every tier stayed green while CI was red. `local_ci.sh --dev-mode`
now exists for exactly this, off by default because it recompiles every object with different flags.
**Run it before assuming CI will agree with a local green.**

The lambda was dead, not accidentally unwired: its comment describes a liveness scan that walks every
trailing word and therefore needs to know which opcodes carry literals, but the scans that shipped
are opcode-targeted and cannot make that mistake. Deleted, with a note in its place so nobody adds
one back.

## 4.8 The CI scene-smoketest job tested nothing, for as long as it existed

Same shape as §2's `local_ci.sh` problem, found while checking the workflows. The job checked out the
repo, installed browsers and ran `run_scenes.mjs` — with no exports on disk, no export artifact and
no `godot-demo-projects`. `exports/` is gitignored and nothing is checked in, so all 19 scenes skipped
with `not exported`, `totalFailed` stayed 0, and `process.exit(totalFailed > 0 ? 1 : 0)` exited 0.
Verified rather than inferred: moving `exports/` aside locally and running the job's exact command
gives `0 passed, 0 failed, 19 skipped` and exit 0.

**Fixed in two halves.** `build-webgpu` — the only job with both an editor and a web template — now
clones `godot-demo-projects` as a sibling of the workspace (a plain `git clone`, because
`actions/checkout` refuses a path outside `$GITHUB_WORKSPACE` and `scenes.json` wants a sibling),
exports all 19 scenes and uploads them as the `scene-exports` artifact. `scene-smoketest` gains
`needs: build-webgpu`, downloads that artifact, and runs with **`--require-exports`**, which turns
"no export on disk" from a skip into a failure so the job can never silently pass again.

**CI builds only the dlink template**, and `run_scenes.mjs` used to hardcode
`variant/extensions_support=false`, which would have produced a broken export — the dlink zip carries
a ~53 MB `godot.side.wasm` that the web export plugin only extracts when that flag is on. It is now
derived from whether the template filename contains `.dlink.`, so CI and local runs each get a
coherent preset. Verified locally by exporting a scene with the dlink template (`index.side.wasm`
present) and running it in Chrome: PASS.

**Cost to know about**: each dlink export is ~45 MB `index.wasm` plus ~52 MB `index.side.wasm`, so
`scene-exports` is ~1.8 GB raw, roughly 450-500 MB compressed, at 7-day retention. Every export's
side module is byte-identical, so uploading one copy and fanning it out would halve that; not done,
because a subtle mistake there breaks a job that cannot be tested without pushing.

**None of §4.7 or §4.8 has been observed in a real Actions run** — this fork is local-commits-only,
so every claim here is from reproducing the workflow's own commands locally.

---

## 5. Corrections — things recorded wrongly earlier

These are fixed in TASKS.md but listed here because reasoning from the old versions wastes a session:
1. **Task 43's diagnosis was wrong twice.** The render-thread guard is `ERR_FAIL_COND_V_MSG` and
   prints in every build, so it was never the silent path; and there was no WebGPU limitation — the
   scene was running **OpenGL**, which has no `RenderingDevice`.
2. **The demo scenes were testing OpenGL, not WebGPU** — all ten, silently, because Godot selects the
   web driver from `rendering_method.web` specifically. Fixed in the harness; it immediately exposed
   four real failures.
3. **The R32Float is `RB_TEX_BACK_DEPTH`, not the MSAA-resolved depth.** `demo_3d_particles` sets no
   MSAA and fails identically. MSAA is not implicated at all.
4. **Task 7.10's reason is out of date** (the `WGPUTextureFormat_R16Snorm` enums now exist in
   emdawnwebgpu 6.0.9) but **its conclusion still stands** — a native `R16_SNORM` attempt made things
   worse, because Dawn reports its sample type as `UnfilterableFloat` and the SDF uses a filtering
   sampler.
5. **Task 14's "the remaining stall is the browser's WGSL→pipeline compilation" is wrong.** Shader
   module and pipeline creation are ~10 ms of a ~2000 ms stall.
6. **Task 44's whole Tint story was wrong** (§4.2). The depth promotion came from this driver's own
   post-Tint pass, not from Tint's SPIR-V reader; the shared `OpTypeImage` was a real observation
   with a wrong inference attached. The general lesson: WGSL captured from the *engine* has already
   been through the driver's text passes, so it is not Tint's output — `tint_convert_cli` on the same
   SPIR-V is, and comparing the two is a two-minute check that would have saved two rounds.
7. **The twelve differing BGLs under one label are materials, not specializations.** Every material
   shares one `ShaderRD`, and the label carries only shader name + variant index.
8. **`demo_compute_heightmap`'s "readback never completes" was wrong** (§4.6). The readback completes
   fine; the one-shot consume-and-requeue design of the driver's cache means a *single* call can never
   see it, so the priming loop ate its own result. The general lesson: before believing a recorded
   "this never works", run the committed export under `capture_errors.mjs --all` and check that the
   failure line the notes describe is actually the one being printed. Here it was not, and the check
   cost thirty seconds and no rebuild.

---

## 6. Build and environment state — read before rebuilding

- **`bin/` is current**: both `godot.linuxbsd.editor.x86_64` and
  `godot.web.template_release.wasm32.nothreads.zip` were built together, from the Task 45 source but
  before it was committed, so they carry the version hash `2e3ccd321` rather than `HEAD`'s. The pair
  matches itself, the exports in `webgpu_tests/scene_smoketest/exports/` were all made from it, and
  the 19/0/0 results were measured on that set — so `--skip-export` reproduces them as-is. The stale
  hash matters only *when something is rebuilt*: the engine version hash comes from the git commit
  and **baked shader caches are keyed to it**, so rebuilding one of the pair produces a mismatch.
  **Rebuild both, or neither** — and re-export afterwards. The tell is `{baked: 0, translated: N}`
  plus a ~5× slower load, and Task 36's warning names it.
- **Rebuild cost, measured today**: the editor is ~30 s and the web template ~47 s incrementally on
  this machine, so "rebuild both" is a minute, not an afternoon. The exact commands:
  ```bash
  scons platform=linuxbsd target=editor webgpu=yes -j$(nproc)     # webgpu=yes = the WGSL baker
  rm -f bin/obj/modules/register_module_types.gen.web.template_release.wasm32.nothreads.o \
        bin/obj/modules/libmodules.web.template_release.wasm32.nothreads.a   # Task 40, see below
  source ~/emsdk/emsdk_env.sh
  scons platform=web target=template_release webgpu=yes opengl3=no threads=no -j$(nproc)
  ```
  Build the **web one last**, so the stale-object trap below lands on the next rebuild rather than on
  the template that is about to be shipped into an export.
- **Interleaving editor and web builds in one tree ships a broken template** (Task 40). The
  `register_module_types.gen` object goes stale and the export dies in `callMain()` with
  `TypeError: resolved is not a function`. Workaround:
  `rm -f bin/obj/modules/register_module_types.gen.<platform>.<target>.*.o bin/obj/modules/libmodules.<...>.a`
  then rebuild. Check with `grep -ac initialize_betsy_module bin/godot.side.web.*.wasm` (want 0).
- **A stale `.godot` in a demo project kills its export.** Left by an editor built at a different
  version hash, it aborts with `ERROR: Parameter "singleton" is null.  at: is_cmdline_mode
  (editor_node.cpp:6622)` and `Aborted`, which reads like an engine crash and is not one.
  `rm -rf <project>/.godot` fixes it. Rebuilding the editor is what makes it likely.
- **An export made with `--headless` silently skips the shader baker** (15 MB pck instead of 135 MB).
- **A scratch project needs `renderer/rendering_method.web="forward_plus"`** or the export picks
  `opengl3` and none of the WebGPU path runs.
- `godot-demo-projects` is cloned at `/mnt/109313D2109313D2/godot-editors/godot-demo-projects`
  (shallow, `master`). The smoketest expects exactly that path. Leaving it absent makes 11 scenes skip.
- **An export dir is wiped before it is written.** It used not to be, and two things followed: files
  the new export does not produce survived (eight dirs carried a months-old 52 MB `index.side.wasm`
  from a dlink export next to a fresh non-dlink `index.wasm` — inert, but 400 MB of confusion), and a
  *failed* export left the previous one in place so the scene ran the old build and reported PASS.
  That second one is the misleading "benchmark_sprites exports fine after failing" note this file
  used to carry. A failed export now leaves nothing.
- The smoketest's non-dlink template comes from `bin/godot.web.template_release.wasm32.nothreads.zip`.
  `local_ci.sh` builds exactly that one (and the editor) and re-exports from them, so it no longer
  needs a separate build — this used to be the caveat that it built the dlink template instead.

---

## 7. Tools (all committed)

```bash
# Startup phase profiling — needs no engine rebuild, works on any existing export.
cd webgpu_tests/startup_phases
node profile_phases.mjs --dir <export-dir> [--browser firefox] [--warm]
                        [--args --benchmark] [--shader-errors] [--eager-pipelines]
                        [--flush-every-mb N] [--no-console]
node features.mjs                      # WebGPU adapter feature matrix, Chrome vs Firefox
node shot.mjs <export-dir> <out.png> <wait-ms> [chromium|firefox]
node trace_block.mjs --dir <export-dir>   # Chrome GPU trace around the longest block

# Scene smoketest, now portable off macOS
cd webgpu_tests/scene_smoketest
GODOT_EDITOR_BIN=../../bin/godot.linuxbsd.editor.x86_64 \
GODOT_TEMPLATE_ZIP=../../bin/godot.web.template_release.wasm32.nothreads.zip \
  node run_scenes.mjs --export-only            # re-export all 19
node run_scenes.mjs --browser firefox          # or chrome
node run_scenes.mjs --scenes demo_3d_particles --export --browser chrome   # one scene, fresh export

# Full-text Dawn errors for one exported scene -- the harness truncates to 200
# characters and prints ~100, which cuts off the "While validating ..." chain that
# holds the actual information. Deduped, cause first, cascade last.
node capture_errors.mjs exports/demo_3d_particles [--browser firefox] [--wait 25000]

# SPIR-V investigation
GODOT_DUMP_SPIRV=/tmp/spv ./bin/godot.linuxbsd.editor.x86_64 --path <proj> --quit-after 120
TINT_DEBUG_DUMP_PREPROCESSED=/tmp/out.spv ./bin/tint_convert_cli <file.spv>
```

**Lesson worth carrying**: trace against the **preprocessed** SPIR-V, not glslang's output. They are
different modules, and designing against the wrong one cost this session a full implementation.

**Second lesson, from the round that actually fixed it**: WGSL captured from the *engine* has already
been through the driver's own text passes, so it is **not** Tint's output. `tint_convert_cli` on the
same SPIR-V is. Diffing the two is a two-minute check and it is what finally attributed Task 44
correctly (§4.2).

Also: Playwright cannot drive the user's own Firefox build (it needs its patched one). And the
adapter decides the errors — `capture_errors.mjs` mirrors `run_scenes.mjs`'s three Chrome modes
(`WEBGPU_REAL_GPU=1`, `CI=1`, or neither = the system's own headed Chrome, which is what every
recorded result here was measured on) precisely because a plain `--enable-unsafe-webgpu` launch
reports an entirely different failure for `demo_3d_particles`: "The number of storage textures (6) in
the Compute stage exceeds the maximum per-stage limit (4)", which is that adapter's limit and not the
bug being chased.

---

## 8. Suggested order for the next session

Nothing here is a known bug — every tier is green and nothing is skipped. In rough order of value:

1. **Delete or annotate the dead `_depth_alias` code** — small, and it has already misled a whole
   round (§4.5). While there: the plain `UNIFORM_TYPE_TEXTURE` branch lacks the reverse depth/float
   fallback its combined-sampler sibling has (Task 24), which is why §4.1 surfaced as a hard Dawn error
   rather than quietly wrong pixels. Adding it is robustness, not a fix — and it would have *hidden*
   §4.1, so add it only with that understood.
2. **Task 14 subtask 2 leftovers**: `Servers:Rendering` is ~500 ms and *fixed* for every project, of
   which ~180 ms is our own per-stage WGSL text scanning. Baking that binding metadata into the
   container at export time is the biggest remaining load win and is entirely our own code.
3. **Texture compression as an export option** (Task 39) — desktop is settled (BC, both browsers);
   Safari and mobile are unmeasured, and that is what the option exists to serve.
4. **Audit the rest of the storage-format class** (§4.4): three instances have been found one at a
   time by running scenes. `copy.glsl` is not the only shader with a format-by-variant storage image,
   and a pass over every `layout(<fmt>, set = …) uniform … image*` against what its C++ callers
   actually bind would close the class instead of the next instance.
