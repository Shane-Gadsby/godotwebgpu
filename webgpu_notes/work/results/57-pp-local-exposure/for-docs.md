# For the wiki: Camera Exposure page, new section "Local exposure"

(Engine Additions > Camera Exposure. Text for users; the lead edits it in.)

## Local exposure

A camera has one exposure for the whole frame. Stand in a room with a sunlit window and that exposure has to choose:
expose for the room and the window is a white blank, expose for the window and the room goes black. Your eyes do not
have that problem, and neither do modern games: **local exposure** exposes each part of the frame again by how bright
its surroundings are, so the window comes down and the room comes up while every edge and texture stays put.

It works like Unreal Engine's local exposure (Bilateral method). The frame's brightness is split into two layers:
the *base*, its large-scale brightness with strong edges kept (window, wall, doorway), and the *detail* on top of it
(texture, small shapes). Local exposure squeezes the base toward middle gray and leaves the detail alone. Colors are
kept: only brightness changes.

### How to use it

On any `CameraAttributes` (Practical or Physical), open **Local Exposure** and tick it on. The settings:

| Property | Default | What it does |
|---|---|---|
| `local_exposure_highlight_contrast` | 0.6 | How much of the contrast above middle gray to keep. 1 = untouched; 0.5 = an area 6 stops over middle gray comes out 3 stops over. Lower brings windows and skies down. |
| `local_exposure_shadow_contrast` | 0.8 | The same for areas darker than middle gray. Lower lifts dark rooms. |
| `local_exposure_detail_strength` | 1.0 | Local detail. 1 keeps it; more than 1 exaggerates texture (the "HDR photo" look); less flattens. |
| `local_exposure_middle_gray_bias` | 0 EV | The brightness left alone. Raise it and the whole frame comes out darker, lower it and brighter. |
| `local_exposure_blurred_luminance_blend` | 0.6 | How much of the base comes from a plain blur instead of the edge-aware grid. 0: crispest separation, may show soft bands on smooth gradients. 1: smooth, but bright areas leak a halo onto dark surroundings. |
| `local_exposure_blurred_luminance_kernel_size` | 50 % | Size of that blur, in percent of the frame's width. |

Everything is defined relative to the frame, so the result is the same at 720p, 1080p or 4K.

Local exposure runs after the global exposure, manual or automatic. Auto exposure meters the frame *before* local
exposure, so turn local exposure on and then adjust the global exposure as usual. It works with physical light
units too.

### Example

```gdscript
var attributes := CameraAttributesPractical.new()
attributes.auto_exposure_enabled = true
attributes.local_exposure_enabled = true
attributes.local_exposure_highlight_contrast = 0.5 # Windows and sky down.
attributes.local_exposure_shadow_contrast = 0.7 # Interior up.
$Camera3D.attributes = attributes
```

From a script on the server side: `RenderingServer.camera_attributes_set_local_exposure(rid, true, 0.5, 0.7, 1.0, 0.0)`
and `RenderingServer.camera_attributes_set_local_exposure_blurred_luminance(rid, 0.6, 50.0)`.

### Tips

- Start with the highlight contrast: most scenes need only that. Go below 0.4 and the image starts to look flat
  and "tonemapped".
- If you see a dark or bright rim along a strong edge (a window frame), lower the blurred luminance blend.
- If smooth gradients (a sky, a lit wall) show soft steps, raise it.

### Limits per renderer

| Renderer | Local exposure |
|---|---|
| Forward+ (desktop) | Yes |
| Forward+ on the web (WebGPU) | Yes, same result |
| Mobile | No: its render buffer clips light at 2.0, so there are no highlights to bring down. The setting is ignored (with a one-time warning in the editor). |
| Compatibility | No: it tonemaps while rendering the scene, so there is no step before tonemapping to do it in. Ignored, with the same warning. |

Also not applied to XR (multiview) rendering.

Cost: two small compute passes on a grid of about 30 x 16 x 33 cells plus two texture reads per pixel in the
tonemap pass, about 0.17 ms per frame at 1080p on the web (RTX 4080 SUPER).
