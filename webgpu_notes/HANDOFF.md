# Handoff — WebGPU work in progress

**As of 2026-09-27, after the Task 44 reclassify fix.** Branch `webgpu-4.7.2`, which currently
matches `origin/webgpu-4.7.2` (`24a1272f3e`). The engine work landed in `5f4b63c136` and
`1d5a037218`; `bin/` was built at `5f4b63c136` (§6).

This is a snapshot for picking the work up cold. `webgpu_notes/TASKS.md` remains the living
detail; this file says where things stand, what is verified, what is *mis*-recorded elsewhere, and
which traps cost time. Task numbers below index into TASKS.md.

**The 60-second version**: one scene fails (`demo_3d_particles`), one cause, already isolated —
§4.4 has the full Dawn message and the two files to read. `bin/` is current and needs no rebuild to
reproduce it: `cd webgpu_tests/scene_smoketest && node capture_errors.mjs exports/demo_3d_particles`
prints it in about a minute.

---

## 1. One-paragraph status

The web export's load time and its Firefox support both moved a long way (Tasks 14, 38, 41, 44), and
the test suite now covers roughly twice what it did (Tasks 42, 44). **The depth-texture problem is
fixed** — and it was never Tint's: this driver's own post-Tint WGSL pass
`_reclassify_single_component_depth_textures()` was rewriting the scene shaders' `depth_buffer` to
`texture_depth_2d`. `demo_3d_platformer` and `stress_3d_platformer` now pass (44 errors each → 0).
**One scene is still failing**, `demo_3d_particles`, on a genuinely separate storage-texture format
mismatch — see §4. Everything else below is either landed and verified, or a correction to something
previously written down wrongly.

---

## 2. Verified state, and when it was measured

| tier | result | measured |
|---|---|---|
| `shader_corpus` | **14/14** | 2026-09-27, after the reclassify fix |
| `driver_unit_tests` | **332/0** | 2026-09-27, after the reclassify fix |
| `preprocessing_tests` | **205/0, 1 skipped** | 2026-09-27, after the reclassify fix |
| Scene smoketest — Chrome | **17 pass, 1 fail, 1 skip** | 2026-09-27, after the reclassify fix |
| Scene smoketest — Firefox | **17 pass, 1 fail, 1 skip** | 2026-09-27, after the reclassify fix |
| Resource lifecycle / screenshot comparison | pass | last full `local_ci.sh` |
| Scene smoketest — Safari | skipped | macOS only; never run here |

The three failures were the *same* three scenes in both browsers — `demo_3d_particles`,
`demo_3d_platformer`, `stress_3d_platformer`. Two of them are fixed (§4). The one left,
`demo_3d_particles`, fails in both browsers on one remaining cause, which each browser words
differently (Chrome: `Format (R32Float) … expected to be (RGBA16Float)`; Firefox: `Storage texture
binding 0 expects format = Rgba16float`). Both full 19-scene runs above were made against a freshly
built editor+template pair at this commit, with a fresh export of every scene.

`demo_compute_heightmap` is the 1 skip. It is marked `known_limitation` in `scenes.json` (its own
open problem — the `GradientTexture1D` readback at the end of TASKS.md Task 44).

Two harness quirks seen in that run, neither caused by the fix and neither affecting a result:
`benchmark_sprites`'s export fails on a missing `res://benchmark_profiler.gd` (it then tests the
previous export and passes), and `demo_compute_heightmap` exports successfully but is reported
`SKIP (not exported)` — the `index.html` existence check runs before the `known_limitation` check,
so the reason string is misleading.

**A full `./webgpu_tests/local_ci.sh --no-safari` has not been run since the SDF fix.** It will
report the scene smoketest as failing until §4.4 is fixed; every other tier was green on the last
full run, and all four offline tiers were re-run green individually today. Re-running it is a
reasonable first act, but expect red for a known reason — and note that its rebuild step builds the
**dlink** template while the smoketest exports with the non-dlink one (§6).

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
| — | Block-align compressed texture-to-texture copies | BPTC textures no longer render black |
| 43 | `create_local_rendering_device()` now reports why it failed | Was returning null silently |
| 42/44 | Smoketest: editor/template overrides, generated presets, forced WebGPU renderer, heightmap self-test | Demo tier runs at all, and runs *on WebGPU* |

Reference numbers worth keeping: the user's project loads with a **~1.0 s** cold stall (was ~9.2 s at
the start of this work), `{baked: 360, translated: 0}`, and BC1 texture compression is verified
equivalent to uncompressed at RMSE 0.0006.

---

## 4. The depth-texture bug, fixed — and what is left (Task 44)

### 4.1 What it was
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

### 4.3 The fix
A fifth disqualifying signal in that pass, beside `half` and `dilated`: the exact name `depth_buffer`
in **group 1** (the scene shaders' render-buffers set, names fixed by engine GLSL). The WGSL then
keeps `texture_2d<f32>` and the `.x` swizzle, which is right for the `R32Float` copy. Group 1 gating
keeps `taa_resolve.glsl`'s and `cluster_debug.glsl`'s own `depth_buffer` (both set 0) rewriting as
before; a user material uniform of that name lands in group 3.

**Result**: `demo_3d_platformer` and `stress_3d_platformer` pass with gpu=0 (44 errors each before),
and the whole tier is **17 pass, 1 fail, 1 skip in both Chrome and Firefox** (was 15/3/1).
`shader_corpus` 14/14, `driver_unit_tests` 332/0, `preprocessing_tests` 205/0+1 skip.

### 4.4 What is still open — `demo_3d_particles`, a storage-format mismatch
The only failing scene left, in both browsers (Chrome 39 errors, Firefox 4), one cause with a cascade
behind it:
```
Format (TextureFormat::R32Float) of [Texture (unlabeled 1152x648 px, R32Float)]
  expected to be (TextureFormat::RGBA16Float).
 - While validating entries[0] against { binding: 0, visibility: Compute,
     storageTexture: {format: RGBA16Float, viewDimension: e2D, access: WriteOnly} }
```
then `SetBindGroup(3, [Invalid BindGroup], …)` ×497 and `[Invalid CommandBuffer]` ×497. A write-only
storage texture, group 3 binding 0, compute, screen-sized — the shape of `effects/copy.glsl:60-72`'s
`dest_buffer`, declared `r32f`/`rgba8`/`rg16f`/`rgba16f` by variant. The suspects are
`_promote_storage_format()` (which sets the *texture's* format) and the WGSL format remaps at
`rendering_device_driver_webgpu.cpp:4840-4900` (which set the *layout's*): the code says the two must
agree, and here they do not. Look there, not at the shader.

### 4.5 Ruled out — do not re-investigate
- **Tint**, in every form: the SPIR-V type split, `fix_depth2_images()` (every image is `Depth=0`),
  and the `_depth_alias` clone mechanism (which does not exist in the vendored Tint —
  `wgsl_depth_alias_bindings` and its name-suffix scan are dead code and should be removed).
- **`split_depth_sampled_image_types()`**, the SPIR-V pass designed and reverted earlier: it was
  aimed at a producer that was never producing this. Do not revive it for this bug.
- **Changing `RB_TEX_BACK_DEPTH`'s format** — it is a colour attachment and storage image by design,
  and the binding is polymorphic anyway. TASKS.md Task 44 has the three independent reasons.
- **The BGL/driver scan** is not wrong; it faithfully reports what the WGSL says.

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

---

## 6. Build and environment state — read before rebuilding

- **`bin/` is current**: both `godot.linuxbsd.editor.x86_64` and
  `godot.web.template_release.wasm32.nothreads.zip` were built at commit `5f4b63c136`, so the pair
  matches itself and the recorded scene results were measured on it. `HEAD` has since moved through
  documentation-only commits, which does not matter *until* something is rebuilt: the engine version
  hash comes from the git commit and **baked shader caches are keyed to it**, so rebuilding one of
  the pair produces a mismatch. **Rebuild both, or neither.** The tell is `{baked: 0, translated: N}`
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
- **An export made with `--headless` silently skips the shader baker** (15 MB pck instead of 135 MB).
- **A scratch project needs `renderer/rendering_method.web="forward_plus"`** or the export picks
  `opengl3` and none of the WebGPU path runs.
- `godot-demo-projects` is cloned at `/mnt/109313D2109313D2/godot-editors/godot-demo-projects`
  (shallow, `master`). The smoketest expects exactly that path. Leaving it absent makes 11 scenes skip.
- The smoketest's non-dlink template comes from `bin/godot.web.template_release.wasm32.nothreads.zip`;
  `local_ci.sh`'s own rebuild step builds the **dlink** one, so re-exporting needs the non-dlink build
  made separately.

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

1. **`demo_3d_particles`'s storage-format mismatch** (§4.4) — the last failing scene, one cause, and
   it needs no rebuild to see. Concretely:
   1. `cd webgpu_tests/scene_smoketest && node capture_errors.mjs exports/demo_3d_particles` — the
      export in the tree is current, and this prints the cause first and its 1490-deep cascade last.
   2. Read `_promote_storage_format()` (`rendering_device_driver_webgpu.cpp:3166`, plus its callers at
      `:2245`/`:2379`/`:2437`, which set the *texture's* and the *view's* format) against the WGSL
      format remaps at `:4840-4900` (which set the *layout's*). Both carry comments saying the two
      must agree; the failure is one place where they do not.
   3. The binding is group 3 binding 0, compute, write-only, screen-sized — the shape of
      `effects/copy.glsl:60-72`'s `dest_buffer`, declared `r32f`/`rgba8`/`rg16f`/`rgba16f` by variant.
      Confirming *which* variant is bound (and whether the layout's `rgba16float` came from a remap or
      from the GLSL) is the first fact to establish, and `GODOT_DUMP_SPIRV` + `tint_convert_cli` on
      `CopyShaderRD:*.comp.spv` answers it offline, without a browser.
   4. Note the BGL is **unlabeled**, unlike the scene ones — so it is built somewhere that does not
      label, which is itself a clue about which code path creates it.
2. **Delete or annotate the dead `_depth_alias` code** — small, and it has already misled once. While
   there: the plain `UNIFORM_TYPE_TEXTURE` branch lacks the reverse depth/float fallback its
   combined-sampler sibling has (Task 24), which is why §4.1 was a hard error rather than quiet
   corruption.
3. **Task 14 subtask 2 leftovers**: `Servers:Rendering` is ~500 ms and *fixed* for every project, of
   which ~180 ms is our own per-stage WGSL text scanning. Baking that binding metadata into the
   container at export time is the biggest remaining load win and is entirely our own code.
4. **Texture compression as an export option** (Task 39) — desktop is settled (BC, both browsers);
   Safari and mobile are unmeasured, and that is what the option exists to serve.
