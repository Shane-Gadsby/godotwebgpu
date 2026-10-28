# 67-metahuman-clothing-root-bind: "Skin bind #0 contains named bind 'Root'" on every load of a MetaHuman with clothing

State: open
Owner: worker-67
Claimed:
Needs: build (editor + web template), native run; investigation first
Branch: work/67
Results: webgpu_notes/work/results/67-metahuman-clothing-root-bind/

User request, 2026-10-28, from the camera-sim session. It's an investigation, then a fix. Every load of
camera-sim's subject prints, natively and on web:

```
ERROR: Skin bind #0 contains named bind 'Root' but Skeleton3D has no bone by that name.
   at: _notification (scene/3d/skeleton_3d.cpp:420)
```

Every project that dresses a MetaHuman in clothing exported from Unreal is likely to get it.

## What the camera-sim session found

Project: `/mnt/109313D2109313D2/godot-game-dev/camera-sim`. Subject: `prefabs/ava.tscn`, built on
`scenes/Ava/head.dna` (fork MetaHuman importer, `metahuman/include_clothing=false`). The clothing comes in
separately as `scenes/Ava/Clothing/outfits.fbx` (Unreal's MetaHuman clothing export), instanced under Ava's
`Skeleton3D`.

Probed with `ResourceLoader` + `Skeleton3D.find_bone()` over every skinned MeshInstance3D in the instantiated
prefab:
- the DNA skeleton has 1185 bones; bone 0 is **`root`** (lowercase), the parent of `pelvis`;
- every MetaHuman mesh (body, head, teeth, eyes, hair, brows, lashes) binds 1185 names, all found;
- the clothing meshes `Ava_Outfits2` (owner: the `outfits` FBX instance) and `Ava_Outfits3` (a copy saved
  into `ava.tscn`, `Skin_ed6hd`) bind 342 names, all found **except bind 0, `&"Root"`**: capital R, identity
  pose, `bone = -1`.

So the FBX's skeleton calls its root joint `Root`, and the DNA's `root`. `Skeleton3D` resolves skin binds by exact
name, so bind 0 fails and the error repeats on every load.

## To investigate

1. Where `Root` comes from: Unreal's FBX export (the armature or scene root written as a joint), or the FBX
   importer adding it. Check another Unreal MetaHuman clothing FBX if one is available.
2. Whether any vertex of the clothing is weighted to bind 0. If none is, the error is harmless but noisy; if
   some are, those vertices currently follow nothing (bind skipped), which would be a visible bug at the hips.
3. Whether the importer's own clothing path (`metahuman/include_clothing=true`,
   `editor_scene_importer_metahuman.cpp` around its "clothing's skeleton differs from the DNA's" check) hits the
   same thing, and what it does with mismatched names.

## Goal (pick after investigating)

- In the MetaHuman importer's clothing path, map clothing binds to the DNA skeleton tolerantly: an exact match
  first, then case-insensitive (`Root` → `root`). Drop a bind that's still unmatched **and** has no weighted
  vertices, rather than keeping a dangling one. Report anything that's unmatched and weighted, once, at import,
  naming the bind.
- For clothing attached by hand (camera-sim's case: the FBX instanced under the DNA skeleton), document the
  supported workflow. If it's worth it, give the FBX importer an option to rename the root joint, or let the
  MetaHuman importer import a clothing FBX against a given `.dna`.
- If the Skeleton3D error itself is misleading for an unweighted bind, consider making it a warning naming the
  mesh. Don't hide it, though: unmatched weighted binds are real bugs.

## Verification

- camera-sim loads with no `Skin bind` error, natively and on web, and the clothing deforms exactly as before:
  screenshots of the idle animation at the same seed (`tools/ui_harness.gd` with `--fixed-fps 60`) are identical
  around the hips and waist.
- A clothing FBX with a genuinely missing, weighted bone still reports it, at import, by name.
- The importer's `include_clothing=true` path tested with the same `outfits.fbx`.

## camera-sim afterwards

Hand this back to the camera-sim session when it lands. If the fix is in the import, it will re-import
`outfits.fbx`/`head.dna` and re-save `prefabs/ava.tscn` in the editor (which also refreshes the stale
`Ava_Outfits3` copy). If it's a documented workflow change, it will follow it. Its CLAUDE.md lists this error as
"expected"; that note goes.

## Added by the lead

- Read TASKS §2.33 and §2.46 and the MetaHuman rules in CLAUDE.md first. Test with `webgpu_tests/metahuman/run.sh`
  where it covers the clothing; the user's MetaHuman export is not in the repo (ask in for-tasks.md if you need a path).
- camera-sim (`/mnt/109313D2109313D2/godot-game-dev/camera-sim`) is read-only: read it as the reference, never modify it;
  scratch copies only.
- **Test scope (user, 2026-10-28):** run only the tests directly related to this task (its Verification items, the
  default/bit-identical check, the dev_mode editor build, the web `template_release` compile); no full Forward+
  matrix, scene smoketest or perf_guard. smoke.sh only if a shader interface changes. Say in SUMMARY.md what you skipped.
- Put the "camera-sim afterwards" hand-back, updated with what you built, in for-tasks.md under "Note for the
  camera-sim session".
