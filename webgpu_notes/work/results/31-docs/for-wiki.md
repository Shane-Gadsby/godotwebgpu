# For the wiki (task 31, docs pass of 2026-10-26 evening)

The wiki clone (`/mnt/109313D2109313D2/godot-editors/godotwebgpu.wiki`) already carries everything tasks 24-30 changed
for users: the Camera White Balance page with the final units (2000-10000 K, tint -150..150), the update-check bullet on
`Engine-Additions.md`, the Notable Fixes rows (FSR 2 web export, DOF/glow/SSR/auto exposure NaN, MetaHuman strands,
MetaHuman skin), and `skin_detail_size` plus the lips limitation on the MetaHuman importer page. Nothing in it is stale.

Two optional additions:

1. **`Engine-Additions-Shader-Baking.md`**, a bullet in the section that explains what the bake does (before the
   "Export caches" bullets):

   > - **The bake answers as the target.** While shaders are baked for the web, the engine's capability queries answer
   >   as WebGPU (no FP16, no image atomics, ...), so the baked variants are the ones the browser will ask for. Those
   >   answers apply only inside the baker; the editor's own viewport keeps rendering with its real GPU during the
   >   export.

2. **`Engine-Additions-MetaHuman-Importer.md`**, the limitations list, after the lips bullet (line 70):

   > - Clothing normal maps lose their z component (the clothing shader reads the blue channel, which Godot's RG normal
   >   map compression sets to 0), so clothing normal detail does not render as authored. A fix is planned.

   Drop it if the cloth fix (TASKS.md §2.46) lands first.
