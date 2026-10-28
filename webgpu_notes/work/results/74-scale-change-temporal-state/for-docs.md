# Task 74: for the shared docs (lead copies in)

## CLAUDE.md, Architecture (one bullet under "Key architectural decisions", or a short paragraph after it)

- **Reconfiguring render buffers keeps temporal state** (work task 74): `RenderSceneBuffersRD::configure()` (a
  `scaling_3d_scale` change, a resize, an MSAA toggle) calls each `RenderBufferCustomDataRD::free_data_for_reconfigure()`
  instead of `free_data()`. Its default frees everything, as before; Luminance (auto exposure's adapted 1x1),
  CameraMetering (auto white balance state) and Forward+'s data (the FSR 2 context and its history) keep what does
  not depend on the internal size. TAA's history and previous velocity are carried as named textures and resampled
  into the new size by `TAA::process()`. A new piece of temporal per-viewport state that must survive a 3D scale
  change overrides `free_data_for_reconfigure()`; `free_data()` must still free everything (it runs on destruction).
  The FSR 2 context keeps the largest render size the viewport has used; a larger one grows it once, copying its
  history over (`FSR2Effect::transfer_history()`). A new target size or leaving FSR 2 still restarts it.

## webgpu_site/ (ARCHITECTURE_AND_DESIGN.md or PERFORMANCE_AND_OPTIMIZATION.md, wherever dynamic resolution is described)

Changing the 3D resolution scale at run time no longer resets auto exposure, FSR 2 or TAA: the luminance
continues from its last value, FSR 2 keeps its context and accumulated history across scale changes, and TAA's
history is resampled to the new size. This holds for every scale change, scripted or from the coming dynamic
resolution controller. Volumetric fog (temporal reprojection) and SSIL's previous frame still restart, as before.

## Wiki (Engine Additions), if the user wants it user-facing

"Changing `Viewport.scaling_3d_scale` while the game runs no longer makes auto exposure flash or FSR 2/TAA shimmer
for a few frames." (A behavior change of upstream Godot, which resets all three.)
