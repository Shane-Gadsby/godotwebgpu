# Wiki text: Motion blur and rolling shutter (Engine Additions, camera group)

## Motion blur and rolling shutter

Real cameras record light over time. While the shutter is open, anything that moves across the frame is smeared
along its path (motion blur), and most CMOS sensors read the image out row by row, so each row is a slightly later
moment than the one above it (rolling shutter): a fast pan leans vertical lines, a passing car skews.

Both are camera properties on `CameraAttributes`, so `CameraAttributesPractical` and `CameraAttributesPhysical` both
have them. Put the attributes on a `Camera3D`, or on the `WorldEnvironment` to apply them to every camera.

### Motion blur

The blur length is how far something moves on screen during the exposure:

| Setting | Exposure time | Example at 60 FPS |
|---|---|---|
| `motion_blur_shutter_angle` (both classes) | angle / 360 of each frame | 180° (default, cinema look): half a frame's motion |
| `CameraAttributesPhysical.exposure_shutter_speed` (default for Physical, `motion_blur_use_shutter_speed`) | 1 / shutter speed seconds | 1/50 s: 1.2 frames' worth of motion; 1/2000 s: almost none |

A shutter angle gives the same look at any frame rate. A shutter speed behaves like a real camera: the blur is
speed x exposure time, whatever the frame rate. Camera motion (pans, dollies, shake) and moving objects both blur, each
by its own speed, and the sky blurs with the camera's rotation. An object the camera follows stays sharp while the
background streaks.

Properties:

- `motion_blur_enabled`: off by default. When off, nothing is computed and the image is unchanged.
- `motion_blur_shutter_angle`: 1-360 degrees, default 180 (hidden on Physical while it uses the shutter speed).
- `motion_blur_strength`: multiplier on the blur length (1 = what the exposure gives).
- `motion_blur_max_length`: the longest blur, as a fraction of the frame height (default 0.05). Very fast motion,
  cuts and teleports are clamped to it. Larger values cost more.
- `CameraAttributesPhysical.motion_blur_use_shutter_speed`: on by default. Works with or without physical light units.
- Project Settings `rendering/camera/motion_blur/motion_blur_quality`: Low (8 samples per pixel), Medium (16, default),
  High (32). Long blurs show fine noise at Low.

### Rolling shutter

- `rolling_shutter_enabled`: off by default.
- `rolling_shutter_readout_time`: milliseconds from the top row to the bottom row (default 20). CMOS stills and video
  cameras read out in 5-30 ms (phones are slow, cinema sensors with fast readout short); a global shutter is 0.

The middle row shows the frame's moment, so the frame stays centered; rows above it are earlier and rows below later.
With motion blur on too, each row is blurred around its own moment.

### Example

```gdscript
var attrs := CameraAttributesPhysical.new()
attrs.exposure_shutter_speed = 50.0          # 1/50 s
attrs.motion_blur_enabled = true             # length from the shutter speed
attrs.rolling_shutter_enabled = true
attrs.rolling_shutter_readout_time = 25.0    # a phone-like sensor
$Camera3D.attributes = attrs

# A film look at any frame rate instead:
var film := CameraAttributesPractical.new()
film.motion_blur_enabled = true
film.motion_blur_shutter_angle = 180.0
```

### How accurate is it

Measured (task 34): a white square moving at a known speed over black renders a blur within 0.3% of the exact
box-shutter result (speed x exposure time) at 720p and 1080p, the same fraction of the frame at both; a camera
panning at a known rate leans vertical lines by readout time x screen speed within 1.1% (objects) and 0.5% (sky).
WebGPU in Chrome matches native Vulkan.

### Limits per renderer

- **Forward+** (desktop and web/WebGPU): supported. Enabling either effect turns on the motion vectors TAA uses, which
  costs a little in the scene pass. Web exports bake the extra shader variants automatically when the project uses
  these properties.
- **Mobile**: not supported (Mobile renders no motion vectors); the image is unchanged.
- **Compatibility**: not supported; the image is unchanged.
- Like every screen-space motion blur: transparent objects blur with what is behind them; each pixel blurs along the
  dominant motion of its neighborhood (two objects crossing in different directions near each other share one
  direction); content a moving object uncovers is estimated from neighboring pixels, and the rolling shutter repeats
  the nearest pixels there. A rotating object blurs along straight lines, not arcs.
