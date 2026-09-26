# Handoff — WebGPU work in progress

**As of 2026-09-27.** Branch `webgpu-4.7.2`, working tree clean, 16 commits ahead of
`origin/webgpu-4.7.2` (nothing pushed — this fork never pushes).

This is a snapshot for picking the work up cold. `webgpu_notes/TASKS.md` remains the living
detail; this file says where things stand, what is verified, what is *mis*-recorded elsewhere, and
which traps cost time. Task numbers below index into TASKS.md.

---

## 1. One-paragraph status

The web export's load time and its Firefox support both moved a long way (Tasks 14, 38, 41, 44), and
the test suite now covers roughly twice what it did (Tasks 42, 44). **One problem is open and
well-understood but unfixed**: three 3D demo scenes fail in both browsers because Tint classifies a
plain `texture2D` as a depth texture, which no configuration change can avoid — see §4. Everything
else below is either landed and verified, or a correction to something previously written down
wrongly.

---

## 2. Verified state, and when it was measured

| tier | result | measured |
|---|---|---|
| `shader_corpus` | **14/14** | after the SDF fix |
| `driver_unit_tests` | **332/0** | after the SDF fix |
| `preprocessing_tests` | **205/0, 1 skipped** | after the SDF fix |
| Scene smoketest — Chrome | **15 pass, 3 fail, 1 skip** | after the SDF fix |
| Scene smoketest — Firefox | **15 pass, 3 fail, 1 skip** | 2026-09-27, latest |
| Resource lifecycle / screenshot comparison | pass | last full `local_ci.sh` |
| Scene smoketest — Safari | skipped | macOS only; never run here |

The three failures are the *same* three scenes in both browsers — `demo_3d_particles`,
`demo_3d_platformer`, `stress_3d_platformer` — and are §4's open problem. Chrome reports 69/44/44
errors, Firefox 3/2/2 for the same cause (Firefox coalesces repeats).
`demo_compute_heightmap` is the 1 skip, marked `known_limitation` in `scenes.json` (§4.3).

**A full `./webgpu_tests/local_ci.sh --no-safari` has not been run since the SDF fix.** It will
report the scene smoketest as failing until §4 is fixed; every other tier was green on the last full
run. Re-running it is a reasonable first act, but expect red for a known reason.

---

## 3. What landed (all verified in a browser unless noted)

| # | Change | Effect |
|---|---|---|
| 14 | Startup phase profiler + `OS_Web` benchmark marks | Load time is now measurable; ~2.0s stall fully decomposed |
| 14 | Glyph-atlas upload coalescing (`frame_pre_draw`) | **~800 ms (41%) off** the user's project; 66→31 MB uploaded before first frame |
| 38 | `lower_subgroup_ops()` SPIR-V pass | **Firefox renders 3D again** (was: UI only, no scene at all) |
| 41 | tier2 storage-format promotion (`rgb10a2unorm` → `rgba16float`) | Firefox Octmap shaders run; **0 validation errors** where there were 14 |
| 44 | Canvas SDF `R16_SNORM` → `R16_SFLOAT` fallback | `demo_2d_particles` **735 errors → 0**, now passes |
| — | Block-align compressed texture-to-texture copies | BPTC textures no longer render black |
| 43 | `create_local_rendering_device()` now reports why it failed | Was returning null silently |
| 42/44 | Smoketest: editor/template overrides, generated presets, forced WebGPU renderer, heightmap self-test | Demo tier runs at all, and runs *on WebGPU* |

Reference numbers worth keeping: the user's project loads with a **~1.0 s** cold stall (was ~9.2 s at
the start of this work), `{baked: 360, translated: 0}`, and BC1 texture compression is verified
equivalent to uncompressed at RMSE 0.0006.

---

## 4. The open problem (Task 44)

### 4.1 Symptom
`demo_3d_particles`, `demo_3d_platformer`, `stress_3d_platformer` fail in Chrome and Firefox:
```
None of the supported sample types (Float|UnfilterableFloat) of [Texture 1152x648 R32Float]
  match the expected sample types (Depth).
 - While validating entries[24] against { binding: 48, sampleType: Depth ... }
 - While validating [BindGroupDescriptor] against [BindGroupLayout "bgl:SceneForwardClusteredShaderRD:19:set1"]
```

### 4.2 Root cause (established, with evidence)
- Binding 48 = GLSL set 1 binding 24 (**the driver doubles every non-combined binding**) =
  `depth_buffer`, declared `uniform texture2D` — a *float* texture.
- SPIR-V deduplicates identical types, so **one `OpTypeImage` is shared by 22 variables**, including
  the shadow atlases (comparison-sampled) and `depth_buffer` (plainly sampled). Depth-ness in SPIR-V
  lives at the *use* site, so this is legal and unambiguous there.
- WGSL puts depth-ness *in the type*, so Tint infers it from usage and promotes variables of the
  shared type. Which ones get promoted depends on which uses survive that conversion, so **the same
  BGL label is built 12 times with different contents** — `entry[24]` is `float` on some creations and
  `depth` on others. Bind groups fail on the `depth` ones.
- Verified by dumping the **preprocessed** SPIR-V (`TINT_DEBUG_DUMP_PREPROCESSED`): every image still
  has `Depth=0` going in, while the WGSL coming out has six `texture_depth` declarations.

### 4.3 Ruled out — do not re-investigate
- **`fix_depth2_images()`** — irrelevant. Every image is `Depth=0`; the pass finds nothing to do.
- **Tint's `_depth_alias` clone mechanism** — *does not exist* in the vendored Tint. The string is
  nowhere in `thirdparty/tint/`, and appears in none of 22 captured WGSL outputs. The driver code
  that looks for it (`wgsl_depth_alias_bindings`, its name-suffix scan, the BGL entries it emits) is
  **dead code**; it misled this investigation and should be removed or annotated.
- **Changing the texture format** — closed for three reasons: the failing texture is
  `RB_TEX_BACK_DEPTH`, a colour attachment *and* storage image by design (so a depth format is
  impossible); the binding is polymorphic anyway (`DEFAULT_RD_TEXTURE_DEPTH` is bound when the copy
  does not exist); and the MSAA path could not have been changed either, since Forward+ resolves
  depth with a compute shader writing `r32f` and WebGPU has no framebuffer depth resolve.
- **The BGL/driver scan** is *not* wrong — it faithfully reports what Tint emitted.

### 4.4 What was attempted and reverted
`split_depth_sampled_image_types()` — clone the ambiguous `OpTypeImage` with `depth=1` plus its
pointer/sampled-image types, repoint the comparison-sampled variables. Compiled, converted cleanly,
and **declined on every real shader**: 9 of 10 comparison samples reach a function-local copy of a
*function parameter*, not a descriptor variable, because `inline_opaque_functions()` has not
flattened those call sites. Recoverable from commit `7a7ca33c6e`'s parent.

Two shortcuts that avoid function-signature work were checked and both fail:
- Mutating the shared type to `depth=1` in place and cloning `depth=0` for plain users →
  `area_light_atlas` is genuinely plain *and* passed to a function, so it would wrongly become depth.
- Cloning `depth=0` for plain users only → invalid SPIR-V (structurally identical duplicate type).

### 4.5 The way forward
A correct split must follow data flow **through `OpFunctionCall` / `OpFunctionParameter`,
`OpStore`/`OpLoad` of local copies, and `OpTypeFunction`**, duplicating any helper called with both a
comparison and a non-comparison texture. That is a type-inference-and-specialization pass — day-scale.

**Before committing to that, spend an hour on the narrow version**: the goal is only to stop *this
binding* being classified as depth. WebGPU's `unfilterable-float` sample type accepts **both** a
colour `R32Float` and a depth-format texture, and this binding is sampled with `SAMPLER_NEAREST_CLAMP`
— so one layout can serve both cases if the WGSL stops saying `texture_depth_2d`. A pass that only
splits variables which are *never* comparison-sampled *and* never cross a function boundary may be
enough, and is far smaller.

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

---

## 6. Build and environment state — read before rebuilding

- **Binaries in `bin/` were built at commit `93f452121c`**; `HEAD` has since moved through
  documentation-only commits. The engine version hash comes from the git commit, and **baked shader
  caches are keyed to it**, so rebuilding *one* of the editor/template pair now produces a mismatch.
  **Rebuild both, or neither.** The tell is `{baked: 0, translated: N}` plus a ~5× slower load, and
  Task 36's warning names it.
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
  node run_scenes.mjs --export-only            # re-export all 18
node run_scenes.mjs --browser firefox          # or chrome

# SPIR-V investigation
GODOT_DUMP_SPIRV=/tmp/spv ./bin/godot.linuxbsd.editor.x86_64 --path <proj> --quit-after 120
TINT_DEBUG_DUMP_PREPROCESSED=/tmp/out.spv ./bin/tint_convert_cli <file.spv>
```

**Lesson worth carrying**: trace against the **preprocessed** SPIR-V, not glslang's output. They are
different modules, and designing against the wrong one cost this session a full implementation.

Also: Playwright cannot drive the user's own Firefox build (it needs its patched one), and raising
the console-capture truncation is what made Dawn's `While validating …` chain visible — without it
the errors are unreadable.

---

## 8. Suggested order for the next session

1. **Task 44's narrow split** (§4.5) — the only thing standing between the suite and green.
2. **Delete or annotate the dead `_depth_alias` code** — small, and it has already misled once.
3. **Task 14 subtask 2 leftovers**: `Servers:Rendering` is ~500 ms and *fixed* for every project, of
   which ~180 ms is our own per-stage WGSL text scanning. Baking that binding metadata into the
   container at export time is the biggest remaining load win and is entirely our own code.
4. **Texture compression as an export option** (Task 39) — desktop is settled (BC, both browsers);
   Safari and mobile are unmeasured, and that is what the option exists to serve.
