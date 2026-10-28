# Task 60: the user's two answers to task 58's swizzle questions

Branch `work/60`: code commit `0b95060385`, webgpu-4.8 merged (for `53719ec67f`, the release-template include fix),
then this results commit. Both answers built as picked; native unchanged. `for-tasks.md`: §4 entry, one §3 trap
candidate, no questions. `for-docs.md`: wiki text replacing "One difference remains", plus the Texture2DRD line.

## What changed

- **`RDD::API_TRAIT_TEXTURE_VIEW_SWIZZLE`** (default 1 in `rendering_device_driver.cpp`; WebGPU 0), read as
  `RenderingDevice::supports_texture_view_swizzle()` and cached by `TextureStorage`. Everything below is gated on it,
  not `#ifdef`; task 58's `#ifndef WEBGPU_ENABLED` around the opaque view swizzle (`_update_render_target()`) is now it.
- **Texture2DRD over R8/RG8** (and TextureLayeredRD / Texture3DRD, same `_texture_format_from_rd()`): `FORMAT_R8` /
  `FORMAT_RG8` with an identity view where views cannot swizzle, instead of L8 / LA8. Class-reference `[b]Note:[/b]`
  in all three.
- **Opaque render targets without view swizzle** (`render_target_needs_opaque_alpha()`) get alpha 1 written by: the
  tonemap pass (Forward+ `TONEMAP_FLAG_OPAQUE_ALPHA`, flags bit 25; Mobile `opaque_alpha` replacing the push constant's
  `pad`, set in the subpass path too; sizes unchanged), both clears of the target, and a canvas draw list into it that
  drew an add/sub/mul/disabled blend batch, which ends with one alpha-only full-target draw
  (`CopyEffects::set_alpha_one_in_drawlist()`: the existing `copy_to_fb` set-color variant with a write mask, no new
  shader). Mix and premultiplied-alpha batches keep alpha 1, so ordinary 2D pays nothing.

## Verification (4080 SUPER; adapter asserted every run: Chrome nvidia/lovelace, 48 sampled textures, not fallback; Firefox non-null)

- Builds: editor `dev_mode=yes` (werror) clean; normal editor; web `template_debug` nothreads; **web `template_release`
  (`dlink_enabled=no webgpu=yes opengl3=no threads=no gdscript_braces=yes module_raycast_enabled=yes`) compiled on the
  merged tree, rc 0.**
- Scratch project `alpha_proj/` (`run_alpha.mjs` captures the web canvas, `celldiff.py` compares cells): 8 cells drawn
  with blending over pure red: 3D opaque / transparent SubViewports (clear alpha 0.5, an alpha-0.5 material), 2D opaque /
  transparent (sub + `blend_disabled`), 2D opaque mul, 2D opaque mix-only, Texture2DRD R8 through a ShaderMaterial,
  Texture2DRD RG8.
  - Native, pre-change vs post-change editor: **0 px** (RGBA, all cells) in Forward+, Mobile and Compatibility; also
    0 px for a Forward+ variant with 4x 2D/3D MSAA and HDR 2D on every viewport.
  - WebGPU vs native, viewport cells 0-5: before, opaque cells differed by up to 230/255 (red through; Forward+ and
    Mobile); after, **max difference 0** in Chrome Forward+ and Mobile, Firefox Forward+ and Mobile, and Chrome
    Forward+ with MSAA + HDR 2D. Transparent cells identical before and after.
  - Texture2DRD on the web: `RS.texture_get_format()` 2 / 3 (R8 / RG8), `get_image()` R8 (0.6, 0, 0, 1) / RG8
    (0.6, 0.784, 0, 1), drawn (153, 0, 0) / (153, 200, 0); the driver's dropped-swizzle warning 0 times (2 before:
    R8 RRR1, R8G8 RRRG). Natively unchanged (L8 / LA8, grey).
  - Native `get_image()` of an opaque viewport reads alpha 1 even with clear alpha 0.5 (2D-only viewport, measured);
    the web now stores 1, so they agree (web readback not separately checked).
- Forward+ matrix `--parity` (fresh exports from this tree's template_debug), arms tonemap, tonemap_custom, glow,
  local_exposure, fxaa, smaa, scaling_3d, msaa, image_formats: **9/9 unbaked, 9/9 baked**, same numbers both (native
  diff off/on: tonemap 0.002/0.003, tonemap_custom 0.002/0.002, glow 0.002/0.002, local_exposure 0.002/0.004, fxaa
  0.002/0.003, smaa 0.002/0.003, scaling_3d 0.002/0.002, msaa 0.002/0.002, image_formats 0.002/0.003); baked 443,
  translated 157. A first baked attempt hung in the native reference render (godot stuck after the tonemap arm 10 min,
  while smoke.sh ran in Chrome); killed, rerun passed.
- smoke.sh on the merged tree:

```
identical interface: 311/311, mismatched: 0, missing in candidate: 0, not in reference: 0
against the stored reference: 622/622 identical, 0 differ, 0 missing, 0 not in reference
validation: 311 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261028-175853-2471499)
```

Interface and text-scan references unchanged, not regenerated.

## For the lead

- Not run: the whole Forward+ matrix, Firefox on the matrix, scene smoketest, perf_guard (normal content adds nothing
  per frame on the web: one tonemap bit; the extra draw only after alpha-changing canvas batches).
- Found, not mine: `--doctool .` on this tree adds three `panini_*` members to `CameraAttributes.xml` (bound in
  `camera_attributes.cpp`, docs missing on webgpu-4.8); restored here.
- Risk: within one canvas draw list, an item reading `SCREEN_TEXTURE` after an alpha-changing batch sees the unfixed
  (raw) alpha, as natively; alpha is forced to 1 at the end of that draw list.
