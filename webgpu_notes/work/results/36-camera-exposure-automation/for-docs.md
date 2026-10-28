# For the docs (task 36: exposure automation)

Wiki page under **Engine Additions**: "Camera exposure automation" (or a section of the camera page, next to white
balance). Text below is user-facing; the class reference XML is already on the branch.

---

## Camera exposure automation

A camera decides two things on its own when you let it: how bright to make the picture (auto exposure) and what color
the light is (auto white balance). `CameraAttributes` (both `CameraAttributesPractical` and `CameraAttributesPhysical`)
now lets you choose **where** auto exposure looks, the way a camera's metering modes do, and adds **auto white
balance**. Everything is off or at its old behavior by default: a project that does not change these settings renders
exactly as before.

Both run on the GPU every frame and never read anything back, so they cost no stall. They are **Forward+ only**
(including the web export with WebGPU), like auto exposure itself.

### Metering modes (Auto Exposure group)

Turn on `auto_exposure_enabled`, then pick `auto_exposure_metering_mode`:

| Mode | What it meters | Use it for |
|---|---|---|
| **Average** (default) | The whole frame, evenly. Godot's auto exposure as it always was. | Evenly lit scenes. |
| **Center-Weighted** | The whole frame, with 75% of the weight in a central circle half the frame height across (Nikon's classic definition: 75% in a 12 mm circle of a 36x24 mm frame). | A subject in the middle with a bright sky or window around it. |
| **Spot** | Only a small circle: `auto_exposure_metering_spot_size` percent of the frame (default 2.5%, the size of camera spot meters), centered at `auto_exposure_metering_spot_point` (UV, (0.5, 0.5) is the center). | Exposing exactly for one thing: a face in a backlit doorway, a stage performer under a spotlight. Move the point from a script to follow a subject. |
| **Highlight-Weighted** | The brightest part of the frame: the `auto_exposure_metering_highlight_percentile`-th percentile (default 99) is exposed just to the clipping point, so only the brightest 1% may blow out. | Keeping detail in skies, neon, fire or snow, at the cost of darker shadows. |

All modes measure the same thing per pixel (the brightest color channel, in linear light) and share the existing
settings: `auto_exposure_scale` is still the exposure compensation (twice the scale = one stop brighter; with
Highlight-Weighted the default 0.4 puts the chosen percentile exactly at 1.0 before tonemapping), `auto_exposure_speed`
how fast it follows, and the min/max sensitivity the limits. The circles are defined relative to the frame, so the
same scene meters the same at any resolution or window size.

**Example: a backlit interior.** A dark room with a bright window: Average exposes for the window and the room goes
black. Center-Weighted brings the room up when your subject is in the middle; Spot on the subject exposes it exactly;
Highlight-Weighted keeps the view out of the window and lets the room go dark.

```gdscript
var attributes := CameraAttributesPractical.new()
attributes.auto_exposure_enabled = true
attributes.auto_exposure_metering_mode = CameraAttributes.METERING_MODE_SPOT
attributes.auto_exposure_metering_spot_size = 3.0 # percent of the frame
$Camera3D.attributes = attributes

func _process(_delta):
	# Keep the spot meter on the player, as a camera's spot meter follows its focus point.
	var uv: Vector2 = $Camera3D.unproject_position($Player.global_position) / get_viewport().get_visible_rect().size
	attributes.auto_exposure_metering_spot_point = uv.clamp(Vector2.ZERO, Vector2.ONE)
```

### Auto white balance (White Balance group)

Turn on `white_balance_auto`. The camera then estimates the color of the light from the picture and corrects it, so a
gray card under a tungsten lamp comes out gray, and follows the light as it changes (walking from a sunlit street into
a lamp-lit room).

| Property | Default | What it does |
|---|---|---|
| `white_balance_auto` | off | Turns auto white balance on. |
| `white_balance_auto_speed` | 1.0 | How fast it follows a change of light: 1.0 covers 63% of the way in one second, 95% in three. |
| `white_balance_auto_min_temperature` | 2500 K | The warmest light it corrects fully. Warmer light (candles, sunsets) keeps some warmth. |
| `white_balance_auto_max_temperature` | 10000 K | The coolest light it corrects fully. |

How it estimates: it averages the pixels that look gray once corrected, ignoring saturated colors and anything bright
enough to clip, and repeats that every frame, so it settles on the neutral surfaces of the scene (walls, floors, gray
objects) rather than on whatever is most colorful.

The manual `white_balance_temperature` and `white_balance_tint` keep working **on top** of auto white balance, as a
bias: at their defaults (6500 K, 0) they do nothing; set the temperature to 5000 K and the corrected picture comes out
cooler, set it to 8000 K and it comes out warmer, exactly as they shift a picture without auto white balance. When you
turn auto white balance on, it starts from the manual setting and adapts from there.

**Example: a warm interior that should stay a little warm.**

```gdscript
attributes.white_balance_auto = true
attributes.white_balance_auto_min_temperature = 3500.0 # do not fully neutralize tungsten light
attributes.white_balance_temperature = 7000.0 # and bias toward warm, like a camera's "ambience priority" AWB
```

**Limits.** Like every auto white balance that judges the light from the picture, a frame filled with one color (a
forest, a field of red flowers, a sunset sky) is partly read as colored light and pulled toward gray. Narrow the
temperature range for such scenes, or keep the manual white balance.

### Renderer support

| | Forward+ (desktop, WebGPU) | Mobile | Compatibility |
|---|---|---|---|
| Metering modes | yes | no (auto exposure is Forward+ only) | no |
| Auto white balance | yes | no: the manual white balance applies alone | no: the manual white balance applies alone |

A warning in the editor says so when you turn them on in Mobile or Compatibility.
