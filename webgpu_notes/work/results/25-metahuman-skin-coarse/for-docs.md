# For docs

**Wiki, Engine Additions, the MetaHuman importer page.** Add to the import options list:

> `metahuman/materials/skin_detail_size` (default 0.012 m): how much skin one repeat of the tiling pore normal covers.
> The importer scales the map to each skin mesh's UV density, so the face and the body get pores of the same size.
> The detail fades out with the mesh's first LOD, as in Unreal.

**CLAUDE.md, "MetaHuman importer", "Rules that cost time":** add one bullet:

> The skin's pore normal tiles in UV space, and body UVs are 3.4x coarser than the face's: scale the tiling from the
> mesh, never with a constant (§2.33).

The class reference (`EditorSceneFormatImporterMetaHuman.xml`) is already updated on work/25.
