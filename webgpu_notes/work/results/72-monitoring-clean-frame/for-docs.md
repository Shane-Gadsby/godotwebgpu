# Task 72: wiki text for the Camera Finishing and Monitoring page

Add as a new section after "Reading the histogram from a script" (task 62).

---

## Saving the picture without the overlays

The monitoring overlays are drawn into the camera's image, so a screenshot of the viewport shows the zebras, false
color, focus peaking and histogram. To save or show the picture the camera *records* while its monitor shows the
overlays, ask the camera for its **clean frame**:

| Camera3D | Default | What it does |
|---|---|---|
| `monitoring_clean_texture_enabled` | `false` | While on, every frame the camera renders is also copied, without the overlays, into a texture. |
| `get_monitoring_clean_texture()` | | That texture (a `Texture2D`), or `null` while off. |

```gdscript
@onready var camera: Camera3D = $SubViewport/Camera3D

func save_photo():
    camera.monitoring_clean_texture_enabled = true
    var image: Image = await camera.get_monitoring_clean_texture().get_image_awaitable()
    camera.monitoring_clean_texture_enabled = false
    if image:
        image.save_png("user://photo.png")
```

Turning it on and reading it in the same function works: the read happens after this frame has been drawn. Without
`await`, use `get_image_async(callback)` and turn it off in the callback. The texture can also be shown directly, in a
`TextureRect` for example, as a clean feed beside the monitor.

**What is in it.** The finished 3D image: exposure, depth of field, white balance, grain, tonemapping, and the
CameraAttributes noise reduction and sharpening. **Not** in it: zebras, false color, focus peaking, the drawn histogram,
and any 2D drawn over the viewport. It is exactly what the viewport's own texture shows with the overlays off, at the
same size and in the same format.

**What it costs.** Nothing while off. While on, one texture the size of the viewport and one copy per rendered frame,
whether or not an overlay is on. Turn it on only while you need it; turning it off frees the texture (a script that
kept it then reads `null` from it). It works whatever **Visible in Output** says.

**One per camera.** Two viewports with their own cameras each have their own clean frame. A camera rendered into two
viewports keeps the latest one's. Until the camera has rendered, the texture is a small placeholder; a viewport that
stops updating keeps its last frame.

**Limits per renderer.** Forward+, Mobile and Compatibility, desktop and web (WebGPU). Not in XR (multiview).

`RenderingServer` has the same as `camera_set_monitoring_clean_texture(camera, texture)`: the renderer copies into
`texture` (a texture you created, for example with `texture_2d_placeholder_create()`) until you set an invalid RID.
