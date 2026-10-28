# For the docs: task 67

## Wiki, `Engine-Additions-MetaHuman-Importer.md`

Add a section after "Import options" (before "Classes"):

```markdown
## Clothing imported on its own

With `metahuman/include_clothing` on, the importer binds the clothing to the DNA skeleton for you. To dress a
character by hand instead — an outfit FBX instanced under the `Skeleton3D` of `head.dna`, for example to keep
your own materials — tell the FBX which character it is worn by. Select the FBX in the FileSystem dock, and in the Import
dock set the `metahuman/bind_to_dna` option to the character's `head.dna` (or `body.dna`), then click **Reimport**.

The import then matches every skin bind to the DNA skeleton's bones:

- A bone with the same name matches.
- A name that differs only in case is renamed to the DNA's spelling, in the skin and in the FBX's own skeleton and
  animations. Unreal names the root joint `Root`; the DNA calls it `root`.
- A bind with no DNA bone and no vertices weighted to it is removed.
- A bind with no DNA bone that vertices are weighted to is kept, and the import prints a warning naming the bone
  and how many vertices use it. Those vertices would follow the skeleton's first bone, so rename the bone in the
  source file.

To attach the clothing, set its `MeshInstance3D`'s **Skeleton** to the character's `Skeleton3D`. A clothing mesh
copied into a scene before the reimport keeps its old `Skin` (with `Root`): copy it again, or rename that bind in
the `Skin` resource. Without this option, `Skeleton3D` reports `Skin bind #0 contains named bind 'Root' but
Skeleton3D has no bone by that name` every time the scene loads.
```

In the import options table, nothing changes (the new option is on the FBX's import settings, not the DNA's).

## Engine docs

None: `EditorScenePostImportPluginMetaHuman` is an internal, unregistered class (like the engine's
`PostImportPluginSkeletonRenamer`), so it has no class reference page.
