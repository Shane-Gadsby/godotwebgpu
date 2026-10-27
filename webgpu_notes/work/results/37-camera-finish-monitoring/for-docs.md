# Wiki text: Camera finish (noise reduction, sharpening) and camera monitoring

Proposed page under **Engine Additions**: "Camera finish and monitoring". Two halves: what the camera does to its
image (CameraAttributes), and what its monitor draws over it (Camera3D).

---

## In-camera noise reduction and sharpening (CameraAttributes)

A real camera processes its image before recording it: it reduces noise and sharpens. These two settings do the same,
after tonemapping and on the display image, so they act on everything in the frame, including the grain and digital
sensor noise of **Grain** (and the LUT's output). Both live on `CameraAttributes`, so `CameraAttributesPractical` and
`CameraAttributesPhysical` both have them. Both are off by default and cost nothing while off.

### Noise Reduction

| Property | Default | What it does |
|---|---|---|
| `noise_reduction_enabled` | off | Turns it on. |
| `noise_reduction_strength` | 1.0 | 0-4 (more allowed). How hard it smooths. |
| `noise_reduction_chroma_only` | off | Reduce only color noise, keep luma detail. |

It is an edge-preserving (bilateral) filter on Y'CbCr: small fluctuations are averaged away, large steps (edges) are
kept. Strength scales both how large a fluctuation counts as noise (about 10 8-bit code values at 1.0, 40 at 4.0) and
how far it averages (1.5 pixels of a 1080-line frame at 1.0, 3 at 4.0). At high strength fine texture smears, the waxy
look of a phone at high ISO, as real in-camera NR does. Color noise is reduced twice as hard as luma noise, as cameras
do; **Chroma Only** leaves luma alone, which keeps texture and removes the colored speckle of high-ISO digital noise.

Example: an ISO 6400 night look.

```gdscript
var attrs := CameraAttributesPractical.new()
attrs.exposure_sensitivity = 6400
attrs.grain_enabled = true
attrs.grain_type = CameraAttributes.GRAIN_TYPE_DIGITAL
attrs.noise_reduction_enabled = true
attrs.noise_reduction_strength = 1.0
$Camera3D.attributes = attrs
```

### Sharpening

| Property | Default | What it does |
|---|---|---|
| `sharpening_enabled` | off | Turns it on. |
| `sharpening_amount` | 0.5 | 0-4. How much contrast is added at edges (0.5 = 50%). |
| `sharpening_radius` | 1.0 | In pixels of a 1080-line frame: scaled with the output, so it looks the same at every resolution. |
| `sharpening_halo` | 0.25 | 0-1. How far past the local range an edge may overshoot. 0 = no halos at all, 1 = a plain unsharp mask. |

An unsharp mask on luma (no color fringes), with the ringing limit of AMD's contrast adaptive sharpening. Small radii
(0.5-1) give crisp detail; large radii (3-5) give local contrast. Keep halo low to avoid the bright/dark outlines of an
over-sharpened video camera, or raise it for exactly that look.

### Limits per renderer

- **Forward+, Mobile, Compatibility, WebGPU (web)**: both work in all of them.
- Not applied in reflection probes or the debug draw modes. Single-view rendering only (not in XR/multiview).
- With a 2D HDR viewport they are applied on the sRGB-encoded image and converted back.

---

## Monitoring overlays (Camera3D)

The exposure and focus aids of a camera's monitor, in Camera3D's **Monitoring** group: zebras, focus peaking, false
color and a histogram. They are drawn over this camera's image, last, under the 2D UI.

**Visible in Output** (default off) decides where they show:
- **Off**: only in the editor, where the editor renders through this camera: the 3D viewport's camera **Preview** and the
  Camera3D inspector preview. A running game draws nothing and does no work for them.
- **On**: also in the running game (for an in-game camera UI, a viewfinder, a filmmaking tool).

### Zebras

Diagonal stripes, crawling as on a camera, over parts of the image above a video level in IRE (percent of full-range
luma, Rec. 709 Y'). `monitoring_zebras_level` (default 95) marks highlights about to clip with black stripes.
`monitoring_zebras_second_enabled` adds a second zebra, white stripes running the other way, over a window of ±5 IRE
around `monitoring_zebras_second_level` (default 70), the classic skin-exposure check.

### Focus peaking

Edges that are in focus are painted in `monitoring_focus_peaking_color` (default red). `monitoring_focus_peaking_sensitivity`
(0-1, default 0.5) sets how weak an edge still lights up. It works on the image's sharpness (the luma gradient), as
camera peaking does, so it shows what depth of field leaves sharp; very low-contrast surfaces show no peaking even in
focus.

### False color

Replaces the image with an exposure map, the ARRI ALEXA false color table:

| IRE | Color | Meaning |
|---|---|---|
| 0-2.5 | purple | black clipping |
| 2.5-4 | blue | just above black |
| 38-42 | green | 18% middle gray |
| 52-56 | pink | one stop over middle gray (light skin) |
| 97-99 | yellow | just below clipping |
| 99-100 | red | clipped |
| other | gray | the image's own luma |

### Histogram

The luma (or R, G, B) histogram of the frame, 256 bins, counted on the GPU each frame, drawn in a corner
(`monitoring_histogram_position`), `monitoring_histogram_size` of the frame width wide and half that high, at
`monitoring_histogram_opacity`. Bars are scaled to the tallest bin.

Example: a monitored camera for a filmmaking tool.

```gdscript
$Camera3D.monitoring_visible_in_output = true
$Camera3D.monitoring_zebras_enabled = true
$Camera3D.monitoring_focus_peaking_enabled = true
$Camera3D.monitoring_histogram_enabled = true
```

### Limits per renderer

- **Forward+, Mobile, WebGPU (web)**: everything.
- **Compatibility**: zebras, focus peaking and false color; **no histogram** (it needs compute shaders, which this
  renderer does not have).
- Overlays are measured on the display image (after tonemapping, LUT, noise reduction and sharpening). Single-view
  rendering only (not in XR).
