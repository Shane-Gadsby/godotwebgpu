# For the docs (work task 70)

Wiki, Engine Additions, the physical depth of field page (task 63's), two additions:

## Maximum blur (project setting)

`Rendering > Camera > Depth of Field > Depth of Field Physical Max Blur Size` is how far a physically sized blur may
reach, as a percentage of the image height. It is **6% by default**: enough for a long lens wide open (200 mm at f/2.8
focused at 5 m blurs the background by 5.6% of the frame). A larger blur is clamped to it.

What it costs (WebGPU, 2560×1440, desktop GPU):

| Shape and quality | 3% | 6% (default) | upstream (32 px) |
|---|---|---|---|
| Hexagon Low | 0.35 ms | 0.42 ms | 0.54 ms |
| Hexagon High | 1.50 ms | 1.54 ms | 1.42 ms |
| Circle Medium | 7.8 ms | **34.7 ms** | 4.0 ms |

- **Box and Hexagon** cost about the same at any value, but spread their samples wider: at 6% each quality draws a
  small blur about as the next lower quality does at 3%, and **Very Low draws a blur under about 1% of the image height
  almost sharp**. Use Low or higher.
- **Circle** (and custom aperture shapes) costs the square of the reach: at 6% about 9 times upstream. Above 1080p,
  use Box or Hexagon in real time, a lower Circle quality, or a smaller value.

## Legacy gather (`dof_legacy_gather`)

`CameraAttributesPhysical > Depth of Field > Legacy Gather` (shown once Physical Blur Size is on). The blur keeps the
lens's size, but it is drawn the way upstream Godot draws its physically based depth of field:

- the gather always reaches 32 pixels (a larger blur clamps there; the project setting above does not apply);
- Box and Hexagon divide by every sample up to that reach, so a blur much smaller than 32 pixels is drawn mostly sharp
  (a 5-pixel blur keeps about a fifth of its width);
- Very Low and Low blur twice as wide as Medium and High.

Use it when a project's look was tuned through upstream's Box/Hexagon DOF and should not change. It is **not
physically correct**: the look depends on the render resolution and on the quality. Circle is affected only by the
32-pixel reach.

Example:

```gdscript
var attributes := CameraAttributesPhysical.new()
attributes.dof_physical_blur_size = true # the lens decides the blur's size
attributes.dof_legacy_gather = true # ...drawn through upstream's gather, as before
$Camera3D.attributes = attributes
```

Renderers: Forward+ and Mobile (Compatibility has no depth of field), web included.
