# Wiki text: physical depth of field blur size (Camera Lens or Bokeh page, Engine Additions)

## Lens-sized depth of field (`CameraAttributesPhysical.dof_physical_blur_size`)

By default a `CameraAttributesPhysical` camera blurs a fixed number of pixels: upstream Godot takes the lens's blur
circle in millimeters, divides it by 5 and treats it as a fraction of a 64-pixel radius. The sensor size and the render
resolution don't enter it, so a smaller sensor shows no more blur than a full-frame one with the same lens, and the
image gets sharper as the resolution goes up.

Turn on **Depth of Field > Physical Blur Size** (`dof_physical_blur_size`) and the blur is the size a real lens
makes. A point at distance *d* blurs into a circle of diameter

    c(d) = f² / (N · (u − f)) · |d − u| / d

on the sensor, with focal length *f* (`frustum_focal_length`), f-number *N* (`exposure_aperture`) and focus distance *u*
(`frustum_focus_distance`). That circle covers the same share of the image as it covers of the sensor:

- **Sensor size counts.** On Micro Four Thirds (13 mm tall) the same lens and aperture blur 24 / 13 = 1.85 times as
  many pixels as on full frame (24 mm tall), as a real crop sensor does.
- **Resolution doesn't change the look.** At 1440p the blur is twice as many pixels as at 720p, so the picture looks the
  same at any resolution, and with 3D resolution scaling or FSR.
- The blur is measured against the sensor height, which is what the field of view is fitted to; for an anamorphic lens
  (`lens_anamorphic_squeeze` above 1) it is measured against the desqueezed width.

Example: a 50 mm lens at f/1.4 focused at 2.5 m, on full frame, rendering at 1280 × 720. A wall at 5 m blurs by
0.5 × 0.729 mm × (2.5 / 5) / 24 mm × 720 = **5.5 pixels** of radius; at 2560 × 1440, 11 pixels.

```gdscript
var lens := CameraAttributesPhysical.new()
lens.sensor_format = CameraAttributesPhysical.SENSOR_FORMAT_FULL_FRAME
lens.frustum_focal_length = 50.0
lens.exposure_aperture = 1.4
lens.frustum_focus_distance = 2.5
lens.dof_physical_blur_size = true
$Camera3D.attributes = lens
```

A 16:9 frame cut from the full width of a sensor (a video mode): give the sensor size as the strip,
`sensor_size = Vector2(36, 36 * 9.0 / 16.0)`, so the vertical field of view and the blur both use its 20.25 mm height.

### How far the blur can reach

**Project Settings > Rendering > Camera > Depth of Field > Depth of Field Physical Max Blur Size** (default 3%) is the
largest blur radius, as a percentage of the image height: 32 pixels at 1080p, 43 at 1440p, 65 at 4K. A bigger blur (a
long lens wide open: 200 mm at f/2.8 focused at 5 m blurs the far background by about 80 pixels at 1440p, 5.6% of the
height) is clamped to it, and looks smaller than it should. Raise the setting for such shots:

- with the **Box** and **Hexagon** shapes the extra reach keeps the number of samples (spread wider, so very small
  blurs look coarser at low quality) and costs a little more: at 1440p on WebGPU (RTX 4080 SUPER) the default Hexagon
  Low pass took 0.61 ms at 3% and 1.33 ms at 10%; Hexagon High 3.2 ms at 3%, 3.4 ms at 10%;
- with the **Circle** shape, and with a custom aperture (`aperture_enabled`), the cost grows with the square of the
  reach: Circle Medium at 1440p took 7.8 ms at upstream's 32 pixels and 13.9 ms at the default 3% (43 pixels); at 6%
  it is too slow to measure. Above 1080p the default already costs the Circle more than upstream does.

### Renderers

- **Forward+** and **Mobile**: full support, the same sizes on both (measured within a few percent of the formula on
  every shape and quality).
- **Web (WebGPU)**: the same as Forward+.
- **Compatibility**: no depth of field at all, so the setting does nothing.

### Notes

- With the setting off, nothing changes from upstream Godot, pixel for pixel.
- With the setting on, the Box and Hexagon shapes also count their samples differently, so a small blur is the full size
  and not mostly the sharp image. Before, with the physical camera's fixed 32-pixel reach, a 5-pixel blur kept only 4 of
  its 25 samples and looked nearly sharp. The Circle shape was already right.
- At **Very Low** quality the Circle shape's first ring of samples sits 8 pixels out, so it cannot draw a blur smaller
  than about 8 pixels (upstream behavior); use Low or better, or the Hexagon.
- Autofocus (`Camera3D.autofocus_mode`) moves `frustum_focus_distance`, and the blur follows it.
