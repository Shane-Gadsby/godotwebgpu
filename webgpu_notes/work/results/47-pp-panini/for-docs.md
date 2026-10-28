# Task 47: text for the wiki's Camera Lens page (Engine-Additions-Camera-Lens.md)

Insert a section after "Diffraction" (before "Example: anamorphic"), and the rows/lines below into "Cost",
"Renderer support" and "Limitations".

---

## Panini projection

On both classes.

| Property | Default | Effect |
|---|---|---|
| `panini_enabled` | `false` | Shows the image through the Panini projection instead of the ordinary (rectilinear) one. |
| `panini_distance` | `1.0` | `0.0` is the ordinary projection, `1.0` the standard Panini projection. In between, a blend. |
| `panini_vertical_compression` | `1.0` | `1.0`: the projection as published, things on the horizon keep their proportions and horizontal lines bow toward the horizon. `0.0`: horizontal lines stay straight, but things at the sides look a little tall. |

A wide field of view (90 degrees and more, common in first-person games) stretches everything at the sides of an
ordinary perspective image sideways. The Panini projection (Sharpless, Ballestrem and Thomas, 2010) keeps vertical lines
and lines through the center straight while showing the sides at their real proportions, so a 120-degree view looks
natural.

The image is zoomed in just enough that no border shows. The horizontal field of view stays the camera's; the vertical
one narrows (at 120 degrees horizontal with `panini_distance` 1.0, the center shows about two thirds of the height it
did). Raise the camera's `fov` to get it back. The center is magnified by the same amount, so it is a little softer.

`Camera3D.unproject_position()`, `project_ray_normal()`, `project_local_ray_normal()` and `project_position()` follow the
projection, so mouse picking and HUD markers placed over 3D objects stay on them. They use the camera's own attributes,
or the World3D's (WorldEnvironment) when the camera has none.

```gdscript
var attributes := CameraAttributesPractical.new()
attributes.panini_enabled = true	# Standard Panini, distance 1.0.
$Camera3D.attributes = attributes
$Camera3D.keep_aspect = Camera3D.KEEP_WIDTH
$Camera3D.fov = 120.0	# Horizontal: the Panini keeps all of it.
```

Cost (add to the Cost section): the Panini projection is a few instructions per pixel in the tonemap pass, like
distortion; on Mobile it turns off the single-pass post-processing, and on Compatibility it turns on the post-processing
pass, as the other lens effects do.

Renderer support (add a row): `| Panini projection | yes | yes | yes | yes |`

Limitations (add):
- The Panini projection applies only to a perspective camera rendering one view: not to orthogonal cameras, and not in
  XR. A frustum camera with an offset is projected around the center of its frame.
- `Camera3D.get_frustum()` and `is_position_in_frustum()` still describe the rendered (wider) frustum, and the camera's
  screen functions do not follow lens distortion (only the Panini projection).
- Like distortion, effects before the tonemap pass (depth of field, SSR, compositor effects) work on the unprojected
  image; that is invisible except that their screen-space sizes are those of the rendered image.
