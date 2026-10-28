# Task 78: a scaling_3d_scale change keeps SSIL's last frame (SUMMARY)

Branch `work/78`, on `webgpu-4.8` at `f3c673deb2`. Renderer only (`servers/rendering/renderer_rd/`, 3 files), no driver, shader or interface change.

## What changed
SSIL and SSR sample `rb_sslf/last_frame`, the previous frame's internal color (6 mips with SSIL). They read it before this frame's copy replaces it. Every `RenderSceneBuffersRD::configure()` freed it, and `SSEffects::allocate_last_frame_buffer()` recreated it cleared to black, so the first frame after a scale change had no indirect light and no SSR.
- `RenderSceneBuffersRD::configure()` carries `rb_sslf/last_frame` over whenever it exists (task 74's `_carry_texture()`).
- `SSEffects::allocate_last_frame_buffer()`: when a carried frame exists, the new texture gets `COLOR_ATTACHMENT`. The carried mip 0 is resampled bilinearly into the new mip 0 with `copy_to_fb_rect()`, as `TAA::process()` does. Mips 1-5 are rebuilt with `make_mipmap()`, the way `copy_internal_texture_to_last_frame()` builds them. With nothing carried it clears as before, so an unchanged viewport gets exactly the texture it always had. The reprojection is in clip/UV space, so the new size needs no change there.
- `RenderForwardClustered::_pre_opaque_render()`: when neither SSIL nor SSR is on, a carried last frame is freed (`free_carried_textures(RB_SCOPE_SSLF)`).

## Verification
The bench is task 74's (`bench/`, plus `ssil=`/`ssr=` switches): HDR float readback, d = mean |frame − previous|, scale changed at frame 0, native Vulkan under Xvfb, `--fixed-fps 60`, 4080 SUPER. "base" is the editor built from f3c673deb2 in this worktree; "new" is this branch. Data is in `data/native/`; tables come from `bench/table.py`.

| run (Forward+) | base: d frame 0 / frame 1, frames to settle | new |
|---|---|---|
| SSIL, bilinear 1.0→0.6 | 0.00389 / 0.00038, 3 | 0.00372 / 0.00000, 1 |
| SSIL, bilinear 0.6→1.0 | 0.00390 / 0.00038, 3 | 0.00372 / 0.00000, 2 |
| SSIL + SSR 1.0→0.6 | 0.00400 / 0.00055, 4 | 0.00372 / 0.00000, 4 (<1e-5 after frame 0) |
| SSR alone 1.0→0.6 | 0.00380 / 0.00018, 3 | 0.00372 / 0.00000, 2 |
| SSIL + TAA 1.0→0.6 | 0.00209 / 0.00022 | 0.00202 / 0.00020 (steady 0.0002) |
| SSIL + TAA 0.6→1.0 | 0.00051 / 0.00031 | 0.00042 / 0.00026 |
| SSIL + FSR 2 1.0→0.6 | 0.00058 / 0.00056 | 0.00052 / 0.00056 (steady 0.0013) |
| reference, SSIL and SSR off | 0.00372 / 0, 0 | identical |

The change frame now differs from the frame before by exactly the bare resolution change, and the frame after it no longer moves. Mean luminance on the change frame goes from −0.2/−0.3 % to −0.1 %, the same as the reference.

**No scale change** (12 frames after 120): Forward+ SSIL bilinear 1.0 and 0.77, SSIL+SSR, SSIL+TAA, SSIL+FSR 2, SSR alone, and Mobile bilinear. Base and new are identical in all 7, 12/12 frames, and the new build run twice is identical.

**Matrix `--parity`** (dev_mode editor + nothreads release template, unbaked, real GPU, adapter asserted): `ssil` PASS (1.744; native 0.002/0.005), `scaling_3d` PASS (1.388; 0.002). Log: `data/matrix_parity.txt`.

**Builds**: editor, dev_mode editor and web template_release all have 0 warnings; pre-commit is clean.

**Skipped**: smoke.sh (no shader or interface change), the web bench capture (the web cannot resolve a pulse this small, per task 74), web template_debug, the full matrix, Firefox.

## Risks
- `_carry_texture()` names the texture as strings (`"rb_sslf"`, `"last_frame"`). Renaming `RB_SCOPE_SSLF` / `RB_LAST_FRAME` without updating it would silently bring back the one-frame reset.
- The first reconfigure adds one bilinear draw per view and 5 small mip dispatches, and from then on the texture is also an attachment.
- SSR-only viewports change behavior too (same buffer), in the direction the user asked for.
- When the view count changes, the carried frame is not used and the texture clears as before.
