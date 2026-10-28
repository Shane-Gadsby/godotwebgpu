# For TASKS.md (task 60)

## §4 archive entry

### 4.xx The web's two swizzle answers: Texture2DRD over R8/RG8, opaque viewports' alpha (October 2026) *(work task 60; user answers `texture2drd-r8-web`, `opaque-subviewport-alpha-web` on task 58's §4 swizzle audit)*

WebGPU texture views cannot swizzle (§3 trap 55), which left two differences after task 58's audit. Both are now closed
the way the user picked, gated on a new driver trait rather than `#ifdef`: **`RDD::API_TRAIT_TEXTURE_VIEW_SWIZZLE`**
(default 1; WebGPU 0), read as `RenderingDevice::supports_texture_view_swizzle()` and cached by `TextureStorage`.
Natively nothing runs differently.

- **`Texture2DRD` (and `TextureLayeredRD`, `Texture3DRD`) over an R8 / RG8 RD texture** is described as `FORMAT_R8` /
  `FORMAT_RG8` with an identity view where views cannot swizzle (`_texture_format_from_rd()`), instead of L8 / LA8 through
  a (R,R,R,1) / (R,R,R,G) swizzle the driver dropped. `RS.texture_get_format()`, `get_image()` and the picture agree
  (web: format R8, pixel (0.6, 0, 0, 1), drawn (153, 0, 0)), and the driver's dropped-swizzle warning no longer fires
  for them (2 warnings before, 0 after). Class-reference `[b]Note:[/b]` in all three classes.
- **Opaque viewports read alpha 1 on the web.** `TextureStorage::render_target_needs_opaque_alpha()` (opaque and no view
  swizzle) makes the passes that finish such a target write alpha 1: the 3D tonemap pass (Forward+
  `TONEMAP_FLAG_OPAQUE_ALPHA`, bit 25 of the push constant's flags; Mobile `opaque_alpha` in the former `pad`; SMAA, scaling
  and FSR 1 after it keep alpha 1), the target's clears (`render_target_do_clear_request()` and the canvas draw list's
  clear), and the 2D canvas: a draw list into the target that drew a batch with the add, sub, mul or disabled blend
  mode ends with one full-target draw that sets alpha to 1 (`CopyEffects::set_alpha_one_in_drawlist()`, the
  `copy_to_fb` set-color variant with an alpha-only write mask; mix and premultiplied alpha keep a destination alpha
  of 1, so ordinary 2D pays nothing). The `#ifndef WEBGPU_ENABLED` task 58 put around the opaque view's swizzle is now
  the trait.

Verification (4080 SUPER; system Chrome with the Vulkan flags and Firefox with the WebGPU prefs, adapter asserted each
run): a scratch project (`results/60-web-swizzle-answers/alpha_proj/`) draws, over pure red with blending, the textures
of a 3D opaque and transparent SubViewport (clear alpha 0.5, an alpha 0.5 material), 2D opaque and transparent ones
(sub + `blend_disabled`), a 2D opaque mul one and a mix-only one, and Texture2DRDs over R8 and RG8.
- Natively, pre-change vs post-change editor, Forward+, Mobile and Compatibility: **0 px different** (RGBA), also
  with 4x 2D/3D MSAA and HDR 2D on every viewport (Forward+).
- WebGPU vs native, all six viewport cells: before, the opaque cells differed by up to 230/255 (red showing through);
  after, **max difference 0** in Chrome and Firefox, Forward+ and Mobile, and with MSAA + HDR 2D (Chrome, Forward+).
  The transparent cells were and stay identical.
- Forward+ matrix with `--parity`, arms tonemap, tonemap_custom, glow, local_exposure, fxaa, smaa, scaling_3d, msaa,
  image_formats: 9/9 unbaked and 9/9 baked (numbers in SUMMARY.md).
- smoke.sh: interface 311/311, text scan 622/622, 311 modules valid; references unchanged.

## §3 trap candidates

- **A blend mode that keeps alpha at 1 is the common case, and it is what made the canvas fix cheap.** With
  destination alpha 1, mix and premultiplied alpha leave it 1 (`sa + 1·(1 − sa)`); only add (above 1 in a float target),
  sub, mul and disabled change it. Count those before reaching for a full-target pass per frame.

## Questions for the user

None.
