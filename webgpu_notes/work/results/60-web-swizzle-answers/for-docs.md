# For the docs (task 60)

## Wiki, Engine Additions → Notable Fixes

Replace the sentence that begins "One difference remains" (opaque viewports keeping their alpha on the web) with:

> **Opaque viewports read alpha 1 on the web, as natively.** A SubViewport without Transparent BG gives a texture
> whose alpha is 1 on every platform. Natively a texture view swizzle does that; WebGPU has no swizzle, so on the
> web the passes that finish an opaque viewport write the alpha themselves: the 3D tonemap pass (Forward+ and
> Mobile), the viewport's clear, and the 2D canvas when an item drawn into it uses the add, subtract, multiply or
> disabled blend mode. A viewport whose clear color or materials lower the alpha now looks the same on the web as
> natively when its texture is drawn with blending, and `get_texture().get_image()` reads alpha 1 on both.
> Transparent viewports are unchanged.

And a line for the same page (or the Texture2DRD entry, if there is one):

> **Texture2DRD over an R8 or RG8 texture on the web.** Natively Godot shows a `RenderingDevice` texture in
> `DATA_FORMAT_R8_UNORM` / `DATA_FORMAT_R8G8_UNORM` wrapped in a `Texture2DRD` (or `Texture2DArrayRD`,
> `Texture3DRD`, ...) as grayscale (`Image.FORMAT_L8` / `FORMAT_LA8`) through a view swizzle WebGPU does not have.
> On the web it is now described as `FORMAT_R8` / `FORMAT_RG8` and drawn that way (red, or red and green, alpha 1),
> so `get_image()`, `RenderingServer.texture_get_format()` and what is drawn agree. For the same picture everywhere,
> use an RGBA8 texture or read the channel in the shader.

## Class reference

Already on the branch: a `[b]Note:[/b]` in `Texture2DRD`, `TextureLayeredRD` and `Texture3DRD` (same text) saying the
above.

## drivers/webgpu/README.md

The "no texture component swizzle" paragraph can add: `RDD::API_TRAIT_TEXTURE_VIEW_SWIZZLE` is 0 on WebGPU
(`RenderingDevice::supports_texture_view_swizzle()`), and shared code uses it for the two cases above
(`TextureStorage::_texture_format_from_rd()`, `TextureStorage::render_target_needs_opaque_alpha()`); the driver's
dropped-swizzle warning now fires only for user `RenderingDevice` views.
