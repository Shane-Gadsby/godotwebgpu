# Wiki text: task 39 (changed and added paragraphs)

Three pages from the camera batch change. Each block below names the page and the paragraph it replaces or follows.

---

## Camera finish and monitoring (task 37's page) -> "### Zebras": replace the paragraph

### Zebras

Diagonal stripes, crawling as on a camera, over parts of the image above a video level in IRE (percent of full-range
luma, Rec. 709 Y'). `monitoring_zebras_level` (default 95) marks highlights about to clip.
`monitoring_zebras_second_enabled` adds a second zebra, stripes running the other way, over a window of ±5 IRE around
`monitoring_zebras_second_level` (default 70), the classic skin-exposure check.

Each zebra has its own stripe color: `monitoring_zebras_color` (default black) and `monitoring_zebras_second_color`
(default white). They are display colors (what you pick is what the monitor shows), like the focus peaking color.
Where both zebras mark the same pixel, the first zebra's color wins. Pick colors that stand out from your scene: black
stripes vanish in a dark-toned look, white ones in a bright one.

```gdscript
$Camera3D.monitoring_zebras_enabled = true
$Camera3D.monitoring_zebras_color = Color.RED              # clipping in red
$Camera3D.monitoring_zebras_second_enabled = true
$Camera3D.monitoring_zebras_second_color = Color(0, 0.5, 1) # skin window in blue
```

The colors work in Forward+, Mobile, Compatibility and WebGPU alike.

---

## Motion blur and rolling shutter (task 34's page) -> add after "### Rolling shutter"'s properties

### In the editor

Both effects come from the camera's motion, so in the editor they would smear and skew the 3D viewport every time you
navigate it. They are therefore **off in the editor** unless you tick **Visible in Editor** in their group:

- `motion_blur_visible_in_editor` (Motion Blur group, default off)
- `rolling_shutter_visible_in_editor` (Rolling Shutter group, default off)

Ticked, the editor viewport and camera previews show the effect like depth of field and grain. In the running project
the tick boxes do nothing: an enabled effect always shows there.

(Also change the "Limits per renderer" or general limits list if it says the editor viewport shows the blur while
navigating: it no longer does, unless ticked.)

---

## Camera3D autofocus and handheld shake (task 38's page) -> "## Handheld shake and stabilization"

Replace the last line of the section ("It runs in the editor's camera preview as well.") with:

**In the editor**, the shake moves the camera preview (the 3D viewport's Preview and the Camera3D inspector preview)
only with **Visible in Editor** (`shake_visible_in_editor`) ticked; by default the camera holds still in the editor so
you can frame the shot, and does no shake work there. The running project always shakes while **Enabled** is on.
Autofocus is not affected: it always runs in the editor.

Add after the property list:

### Presets

**Preset** (`shake_preset`) fills in every shake setting (amplitudes and frequencies of all axes, breathing,
stabilization and Scale With Focal Length; not the seed) for a typical way of holding or mounting a camera. Change any
of them afterwards and the preset switches to **Custom**, keeping the values, so a preset is a starting point. A new
camera starts on Custom with the default values (a camera held in two hands while standing).

Rotation is pitch / yaw / roll in degrees RMS, position right / up / back in meters RMS; each axis moves in a band from
half to twice its frequency. Every preset has Scale With Focal Length on.

| Preset | Rotation (deg RMS) @ Hz | Position (m RMS) @ Hz | Breathing | Stabilization | Where the numbers come from |
|---|---|---|---|---|---|
| Custom | your values | | | | |
| Handheld | 0.12, 0.12, 0.06 @ 3, 3, 2.5 | 0.001, 0.001, 0.0005 @ 0.5 | 0.1° @ 0.25 Hz | 0 | Hand shake of a camera held still: CIPA DC-X011 (image stabilization standard), most energy 1-10 Hz; standing postural sway of a few mm at 0.3-0.5 Hz (Prieto et al. 1996); 15 breaths a minute |
| Handheld Stabilized | as Handheld | as Handheld | as Handheld | 0.8 | Handheld with a lens or in-body stabilizer: the hand-shake band drops to about a fifth (look choice inside the 2-5 stops CIPA ratings report) |
| Handheld Walking | 0.8, 0.6, 0.5 @ 2, 1, 1 | 0.008, 0.012, 0.005 @ 1, 2, 2 | 0.15° @ 0.4 Hz | 0 | Step frequency ~2 Hz (Pachi and Ji 2005) drives pitch and vertical bob; sideways sway, yaw and roll at the stride frequency, half of it; head travel 2-4 cm (Hirasaki et al. 1999), partly absorbed by the arms |
| Running | 2, 1.5, 1.5 @ 2.8, 1.4, 1.4 | 0.015, 0.03, 0.01 @ 1.4, 2.8, 2.8 | 0.3° @ 0.7 Hz | 0 | Running step frequency 2.7-3 Hz (Cavanagh and Kram 1989), larger vertical travel, fast breathing |
| Shoulder Rig | 0.15, 0.2, 0.2 @ 1.2, 1, 0.8 | 0.003, 0.003, 0.002 @ 0.8 | 0.12° @ 0.25 Hz | 0 | Braced on the shoulder: slow body sway instead of hand shake (look choice) |
| Gimbal | 0.03, 0.03, 0.02 @ 0.5 | 0.004, 0.006, 0.003 @ 1, 2, 2 | none | 0 | A gimbal removes rotation (a slow residual drift remains) but not the walking bob, which passes through damped (look choice) |
| Tripod in Wind | 0.03, 0.03, 0.01 @ 10 | 0.0002, 0.0002, 0.0001 @ 10 | none | 0 | No operator; a loaded tripod rings at roughly 8-20 Hz (amplitude a look choice for a light breeze); shows mostly on long lenses |
| Vehicle Mount | 0.3, 0.1, 0.3 @ 1.2, 8, 1 | 0.003, 0.01, 0.003 @ 1, 1.2, 1.2 | none | 0 | Car body bounce, pitch and roll at the ride frequency of 1-1.5 Hz, yaw buzz from wheel hop (Gillespie, *Fundamentals of Vehicle Dynamics*) |

```gdscript
$Camera3D.shake_enabled = true
$Camera3D.shake_preset = Camera3D.SHAKE_PRESET_HANDHELD_WALKING
$Camera3D.shake_breathing_amplitude = 0.3   # Out of breath: the preset becomes Custom, the rest stays.
```

Measured (task 39): each preset's recorded shake over 120 s matches its table values, RMS within 5% (breathing within
12%) and band centers within 10% on every axis.
