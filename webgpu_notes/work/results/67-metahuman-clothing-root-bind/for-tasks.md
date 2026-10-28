# For TASKS.md: task 67

## §4 entry (archive)

### 4.xx MetaHuman clothing: skin binds matched to the DNA skeleton (`Root` vs `root`) `DONE 2026-10-28` *(work task 67)*

camera-sim printed `Skin bind #0 contains named bind 'Root' but Skeleton3D has no bone by that name` on every load,
natively and on web. Cause: Unreal's FBX export (FBX SDK 2020.2, "Unreal Engine 5.8.3") writes the skeletal mesh's
root as a `Null` model named `Root` with a cluster, so Godot's FBX import gives the skeleton a bone `Root` and the
skin a bind `Root`; the DNA calls the same joint `root`. `Skeleton3D` resolves named binds exactly. camera-sim
instances `outfits.fbx` under the DNA's `Skeleton3D` by hand (`metahuman/include_clothing=false`, so its shirt
script can keep `StandardMaterial3D`s), and its copied `Ava_Outfits3` skin kept `Root`.

- **Harmless, but noisy:** 0 of 13,894 clothing vertices are weighted to `Root`, and an unmatched bind already fell
  back to bone 0, which is the DNA's `root`. Nothing deformed wrongly.
- **The importer's own path** (`include_clothing=true`) already matched names ignoring case since phase 1
  (`_find_bone_any_case`) and rebinds to the shared DNA skin, so it never had the error. It now counts weighted
  vertices per bind: an unmatched unweighted bind is `print_verbose` only, an unmatched weighted one is warned once
  per bone, naming the file, bone, mesh and vertex count (it warned for every unmatched bind before, weighted or not).
- **New: the scene import option `metahuman/bind_to_dna`** (`EditorScenePostImportPluginMetaHuman`,
  `modules/metahuman/editor/metahuman_clothing_binds.{h,cpp}`), on every scene import except `.dna`, empty by
  default (no effect, no reimport of existing files). Set to a `.dna` (head DNA: the body DNA beside it, via the
  manifest, is included, as the importer merges them), `pre_process` matches every skin bind: exact; else ignoring
  case, renaming the bind, the scene's own `Skeleton3D` bone and its animation tracks to the DNA spelling; else, with
  no weighted vertex, the bind is dropped and the mesh's bone indices are remapped; else (weighted) kept and warned by
  name with its vertex count.
- Verified on a scratch copy of camera-sim: `outfits.fbx` reimported with `bind_to_dna=head.dna` renames 1 bind
  (`Root`→`root`), drops 0, mesh arrays bit-identical (same vertex/bone/weight hashes); with `ava.tscn`'s copied skin
  updated the same way, a 120-frame run prints 0 `Skin bind` errors (1 before). Against `head.dna` alone (no body
  DNA, so the body bones are genuinely missing): 279 unweighted binds dropped, the 36 weighted missing bones
  (`pelvis` 6,521 vertices, `spine_01`, the thigh and upper-arm correctives, ...) warned once each by name with counts, the skin trimmed 342 → 63 binds and every vertex still weighted to the
  same bone names with the same weights (per-vertex hash equal). `include_clothing=true` on `head.dna`: no warning,
  clothing bound to all 1,185 bones.

## §3 trap candidates

- **An FBX from Unreal names its root joint `Root`; MetaHuman DNA calls it `root`** (2026-10-28). `Skeleton3D`
  resolves named skin binds exactly, so clothing attached to a DNA skeleton by hand logs
  `Skin bind #0 contains named bind 'Root'` on every load (harmless: no vertex is weighted to it, and an unmatched
  bind falls back to bone 0). Import the FBX with `metahuman/bind_to_dna`, or let the DNA import bring the clothing
  (`include_clothing`). A mesh copied into a `.tscn` before that keeps its own `Skin` and its error: copy it again.
- **An import-time skin fix does not reach a `Skin` already copied into a scene** (variant of trap 71): a
  `MeshInstance3D` made local or copied from an imported scene carries the skin as a sub-resource of the `.tscn`,
  which a Reimport never touches.

## Questions for the user

1. **`Skeleton3D` resolving skin binds ignoring case** — a core change that would remove the error for any
   already-copied skin without a reimport, for every project (only when the exact name is missing).
   Choices: (a) no, keep the fix in the import (default); (b) yes, case-insensitive fallback in `Skeleton3D`.
   Default (a): it is upstream behavior, and the import option already fixes the source.

## Note for the camera-sim session

Task 67 landed (work/67). The error came from `Ava_Outfits3`'s copied `Skin` (`Skin_ed6hd` in `prefabs/ava.tscn`),
whose bind 0 is `Root`; the DNA skeleton calls it `root`. No vertex is weighted to it, so nothing ever deformed
wrongly; the fix only removes the error. To apply it, in the fork editor built from this change:

1. Select `scenes/Ava/Clothing/outfits.fbx`, set the import option `metahuman/bind_to_dna` to
   `res://scenes/Ava/head.dna`, and click **Reimport**. The FBX's skin bind and its own skeleton's bone become
   `root`; nothing else changes (vertex, bone and weight arrays bit-identical).
2. `Ava_Outfits3` keeps its own copy of the skin, which the reimport does not touch: in the inspector, open its
   `Skin` and rename bind 0 from `Root` to `root` (or copy the skin from the reimported `Ava_Outfits2` again). Save
   `prefabs/ava.tscn` (this also clears the "recovery process" / `Ava_Outfits2 ... vanished` warnings).
3. Drop the "only expected console error" note from camera-sim's CLAUDE.md (lines ~71 and ~352).

`head.dna` needs no reimport for this. Keeping `metahuman/include_clothing=false` is fine.
