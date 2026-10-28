# Wiki: Engine Additions -> Camera3D autofocus and handheld shake

## Autofocus

Camera3D can focus by itself, like a real camera's autofocus. It changes the focus of the camera's **Attributes**:
the focus distance of a `CameraAttributesPhysical`, or, for `CameraAttributesPractical`, the near and far blur
distances (and their transitions), which move together around the focus point so the sharp zone keeps its depth as a
real lens's depth of field does.

In the inspector, under **Autofocus**:

- **Mode**: *Manual* (off), *Single* (AF-S: focuses each time you call `autofocus_trigger()`, then holds), or
  *Continuous* (AF-C: keeps the subject in focus every frame).
- **Target**: a Node3D to track. Its distance along the camera's view axis is the focus distance.
- **Points**: when there is no target, screen points (0 to 1, `(0.5, 0.5)` = center) where the camera measures
  distance with a physics ray. With several points, the nearest hit wins. **Collision Mask** picks the layers the rays see.
- **Time Constant**: how fast focus moves. The remaining error drops to 37 % each time constant (95 % after three),
  at any frame rate. Focus moves evenly in diopters (1 / distance), so 1 m -> 2 m takes as long as 2 m -> infinity.
- **Hunting**: 0 = smooth (phase-detect style); above 0 the lens overshoots by that fraction and settles back
  (contrast-detect "hunting").

Signals: `focus_acquired(distance)` when the subject is sharp (within a quarter of the depth of field), and
`focus_failed` when there is nothing to focus on. `get_autofocus_distance()` and `is_autofocus_acquired()` give the state.

Autofocus also runs in the editor, so the camera preview shows it. Saving the scene saves the values you typed, not
where autofocus happened to be.

```gdscript
@onready var cam: Camera3D = $Camera3D

func _ready() -> void:
    cam.attributes = CameraAttributesPhysical.new()  # Own resource per camera.
    cam.autofocus_mode = Camera3D.AUTOFOCUS_MODE_SINGLE
    cam.autofocus_points = PackedVector2Array([Vector2(0.5, 0.5), Vector2(0.35, 0.5), Vector2(0.65, 0.5)])
    cam.autofocus_time_constant = 0.15
    cam.focus_acquired.connect(func(d): print("sharp at %.2f m" % d))

func _unhandled_input(event: InputEvent) -> void:
    if event.is_action_pressed("half_press"):
        cam.autofocus_trigger()
```

Limits:
- Autofocus writes into the attributes resource; cameras sharing one resource share its focus. Give each its own
  (or enable *Local to Scene*).
- Ray points need physics on the main thread (not *Physics > 3D > Run on Separate Thread*); a target node always works.
- With CameraAttributesPractical and only far blur enabled, the focus is taken at half the far distance.
- Works the same in Forward+, Mobile and Compatibility and on the web: it only changes the attributes. Visible blur
  needs depth of field, which each renderer supports as before.

## Handheld shake and stabilization

Under **Handheld Shake**, **Enabled** makes the camera move as if held by hand: small rotations and position wobble
plus a slow breathing sway. The node's own transform does not change (gameplay code, parenting and physics keep
working); only the rendered view, `get_camera_transform()` and picking (`project_ray_normal()` and friends) include
the shake. `get_shake_offset()` returns it.

- **Rotation Amplitude** (degrees RMS, pitch/yaw/roll) and **Rotation Frequency** (Hz) per axis; **Position
  Amplitude** (meters RMS) and **Position Frequency** per local axis. Each axis is noise spread from half to twice its
  frequency. Defaults are a camera held in two hands while standing.
- **Breathing Amplitude** (degrees) / **Frequency** (0.25 Hz = 15 breaths a minute).
- **Stabilization** (0-1): image stabilization. Removes that fraction of the shake above about 0.5 Hz, less of the
  slower sway, like optical or in-body stabilization.
- **Scale With Focal Length**: on (default, physical) = amplitudes are angles, so a telephoto (small FOV) shows more
  shake, as on a real camera. Off = the shake keeps its on-screen size when zooming (amplitudes as seen through a
  50 mm full-frame lens).
- **Seed**: the shake is deterministic; cameras with the same seed shake the same way.

```gdscript
$Camera3D.shake_enabled = true
$Camera3D.shake_rotation_amplitude = Vector3(0.5, 0.5, 0.2)  # A nervous operator.
$Camera3D.shake_stabilization = 0.8                           # ... with a stabilized lens.
```

Limits: works identically in every renderer and on the web (it moves the camera, nothing is rendered differently).
It is not meant for XR cameras. It runs in the editor's camera preview as well.
