# Task 67: "Skin bind #0 contains named bind 'Root'" with MetaHuman clothing

Branch `work/67`. State: ready-for-integration.

## Findings

1. **Where `Root` comes from:** the source file. `outfits.fbx` (FBX SDK 2020.2, written by "Unreal Engine
   5.8.3") has a `Null` model `Root` with a skin cluster, parent of `pelvis`; Godot's FBX import makes it bone 0 of
   the FBX's `Skeleton3D` and bind 0 of the skin. The DNA calls that joint `root`. Only one Unreal clothing FBX was
   available, so whether every Unreal export does this is unconfirmed (likely: it is the skeletal mesh's root).
2. **Weighted vertices:** none. 0 of 13,894 clothing vertices (`Ava_Outfits3`, both surfaces) carry weight on bind 0.
   An unmatched named bind already falls back to bone 0 in `Skeleton3D`, which is the DNA's `root`, so nothing ever
   deformed wrongly; the error was noise.
3. **`include_clothing=true`:** never had the problem. `_add_clothing` matched names ignoring case since phase 1 and
   rebinds onto the shared 1,185-bone DNA skin. Reimporting camera-sim's `head.dna` with it on: no warning, the
   clothing mesh binds all 1,185 names.

## What changed

- `modules/metahuman/editor/metahuman_clothing_binds.{h,cpp}` (new): `metahuman_count_bind_weights()` and
  `EditorScenePostImportPluginMetaHuman`, which adds the scene import option **`metahuman/bind_to_dna`** (a `.dna`
  path, empty by default; not offered on `.dna` files). When set, `pre_process` matches each skin bind to the DNA
  skeleton's bone names (that DNA's joints, plus the body DNA beside a head DNA, as the importer merges them):
  exact; else ignoring case, renaming the bind, the scene's own bone and its animation tracks; else, with no weighted
  vertex, dropping the bind and remapping the mesh's bone indices; else keeping it and warning once, naming the bone,
  mesh and vertex count. Registered in `register_types.cpp` (editor only, unregistered class like the engine's
  `PostImportPluginSkeletonRenamer`).
- `editor_scene_importer_metahuman.cpp` `_add_clothing`: unmatched binds are now only `print_verbose`'d when no
  vertex is weighted to them, and warned once per bone (with file, mesh and vertex count) when some are. Before, every
  unmatched bind warned, weighted or not.
- Empty option = no change for every existing import; adding an option does not trigger reimports.

## Verification (scratch copy of camera-sim, `$SCRATCH/w67/cs`, read-only original untouched)

- `outfits.fbx` reimported with `bind_to_dna=res://scenes/Ava/head.dna`: "1 binds renamed, 0 unweighted binds
  dropped"; FBX skeleton bone 0 and skin bind 0 now `root`; vertex/bone/weight array hashes and all 342 bind poses
  identical to the import before the change (diff of the dumps: only the two name lines).
- `prefabs/ava.tscn`'s copied skin (`Skin_ed6hd`) with bind 0 renamed the same way (what the camera-sim session will
  do): 120-frame headless run of the main scene, `Skin bind` errors 1 → 0 (the only other errors, 2x dummy-renderer
  `texture_2d_get`, are in both runs).
- Skinning unchanged: bind 0 resolved to bone 0 before (fallback) and resolves to `root` = bone 0 now, with the same
  bind pose, the mesh untouched and no vertex weighted to it, so the skinned result is identical by construction.
- Real missing bones: `outfits.fbx` bound to `head.dna` alone (no body DNA beside it, so the body bones are genuinely
  missing): 36 weighted missing bones warned at import by name with counts (e.g. `pelvis`, 6,521 vertices), 279
  unweighted binds dropped, skin 342 → 63 binds; a per-vertex hash of (vertex, bind name, weight) over every weighted
  slot is identical to the untouched import, so the index remap is exact.
- `include_clothing=true` on `head.dna`: no warnings, clothing bound (finding 3).
- Builds: `scons platform=linuxbsd target=editor accesskit=yes webgpu=yes gdscript_braces=yes linker=mold
  dev_mode=yes` (werror) OK; `scons platform=web target=template_release dlink_enabled=no webgpu=yes opengl3=no
  threads=no gdscript_braces=yes module_raycast_enabled=yes` OK. `pre-commit` on the changed files passes.

## Skipped (test scope, user 2026-10-28)

- **The seeded `ui_harness.gd` screenshot pair** (hips/waist identical): the `gpu` lease was held by workers 63,
  64 and 62 in turn for the whole session. The scripts are ready: `$SCRATCH/w67/tools/shots.sh <tag>` (seeded copy of
  the harness at `--fixed-fps 60`); run it with `ava.tscn.orig` and with the renamed skin and compare `*_vp.png`.
  The numerical check above already shows the result cannot differ.
- No web run: the fix is in the editor's import only; the web export carries the fixed scene, and the web template
  compile shows nothing in it changed.
- No Forward+ matrix, scene smoketest, perf_guard or smoke.sh (no shader or converter change).
- Animation-track renaming is untested on real data: `outfits.fbx`'s only animation ("Unreal Take") has 0 tracks.

`$SCRATCH` = `/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad`.

## Risks

- A bind is dropped only with the option set, and only when no vertex of any mesh using that skin is weighted to it;
  a mesh shared between two skins is never remapped.
- camera-sim's existing error goes only after its session updates `Ava_Outfits3`'s copied skin (see for-tasks.md,
  "Note for the camera-sim session"); a Reimport alone does not reach a skin copied into a `.tscn`.
