# 65-light-temperature-without-plu: let Light3D.light_temperature colour a light without physical light units

State: open
Owner: worker-64 (with task 64, on work/64)
Claimed:
Needs: build (editor + web template), native Vulkan run, browser (Playwright)
Branch: work/64
Results: webgpu_notes/work/results/65-light-temperature-without-plu/

User request, 2026-10-28, from the camera-sim session. camera-sim uses the engine's white balance
(`CameraAttributes.white_balance_temperature`), but has to colour its lights in GDScript: `light_temperature`
does nothing unless `rendering/lights_and_shadows/use_physical_light_units` is on, and the inspector hides it.
Many projects, camera-sim included, don't use physical light units, because they'd have to re-light every scene
and re-calibrate exposure.

camera-sim's copy is `CinemaSettings.light_color_for_temperature()` (`scripts/camera_settings.gd`). It uses the
same Planckian-locus approximation as `_color_from_temperature()` in `scene/3d/light_3d.cpp`, normalised
differently (see below), and writes the result to `light_color` on its key, fill, rim and room lights.

## The problem

`scene/3d/light_3d.cpp`:
- `Light3D::set_temperature()` returns early without physical light units, so `light_temperature` is stored but
  never applied.
- `_validate_property()` hides `light_temperature` without physical light units.
- `_color_from_temperature()` normalises the colour so **its brightest channel is 1**
  (`linear /= linear[max_axis_index()]`), then clamps. So the light's brightness changes with its temperature:
  a 3200 K light (red 1, blue well below 1) has less luminance than a 6500 K light at the same energy. With
  physical light units the energy is in lumens or lux and this is arguably a bug too, but changing that path
  isn't part of this task.

## Goal

1. **Opt-in, per light:** e.g. `Light3D.light_temperature_enabled` (bool, default `false`). When it's true,
   `light_temperature` tints the light whether or not physical light units are on, and the inspector shows it.
   When it's false, everything behaves exactly as today. With physical light units on, today's always-on
   behaviour stays; document how the two interact (simplest: physical light units imply it, and the property
   only matters without them).
2. **Constant luminance** for the opt-in path: normalise the temperature's colour to **luminance 1** (Rec. 709 Y
   of the linear colour), not to its brightest channel, so changing a light's temperature never changes how
   bright it is. Channels may then exceed 1 (blue at high temperatures); don't clamp, since light colours are
   linear HDR. Then multiply by `light_color` as `set_color()` already does with physical light units.
   Document the normalisation.
3. **The same formula as white balance.** `_color_from_temperature()`, `camera_attributes_storage.cpp`
   (`_white_balance_planckian_uv`) and `camera_metering.glsl` all use the Krystek uv fit. Keep the light using
   that same fit, so a light at T renders neutral under a white balance of T (the docs already promise this).
   Consider moving the fit into one shared helper (see the companion task 64-color-from-temperature).
4. Gizmo colour, `get_correlated_color()` and the editor preview follow the opt-in.

## Verification

- **Neutral pairs:** a white diffuse card lit by one OmniLight3D with the opt-in on, Linear tonemapper, camera
  white balance at the same temperature: the card's linear RGB is neutral within 1% at 2500, 3200, 4300, 5600,
  6500 and 8000 K. camera-sim checked its GDScript version this way.
- **Constant luminance:** same card, white balance off (6500 K): the card's luminance stays within 1% across
  2000–10000 K at a fixed energy.
- **Opt-in off, without physical light units:** bit-identical to today (the temperature is ignored).
- **With physical light units on:** bit-identical to today for lights without the opt-in.
- **Renderers:** Forward+, Mobile, Compatibility and web.
- **Inspector:** `light_temperature` shows when the opt-in is on, without physical light units.

## camera-sim afterwards

Hand this back to the camera-sim session when it lands. It will set `light_temperature_enabled` on
`%KeyLight`, `%FillLight`, `%RimLight` and `%DirectionalLight3D` (the 5600 K room fill), write the temperature
sliders to `light_temperature`, leave `light_color` white, and delete
`CinemaSettings.light_color_for_temperature()`. Its exposure model and light meter assume constant luminance
across temperatures, which point 2 provides.

## Added by the lead

- Tasks 64 and 65 are one worker's, on one branch (`work/64`), in that order: first the shared colour-temperature
  helper and `Color.from_temperature()` (64), with `Light3D` and the white-balance storage calling it and their
  output bit-identical; then the `light_temperature_enabled` opt-in with constant luminance (65) on top of it.
  Separate commits and separate SUMMARY sections; one results folder each.
- Tasks 62 (histogram readback) and 63 (physical DOF blur) run at the same time; stay out of their files
  (CameraFinish, Camera3D monitoring, CameraAttributesPhysical's DOF, BokehDOFRD).
- camera-sim (`/mnt/109313D2109313D2/godot-game-dev/camera-sim`) is read-only: read `scripts/camera_settings.gd` as
  the reference, never modify it.
- Compile a web `template_release` before `ready-for-integration` (work README, TASKS §3 trap 95).
- Put the "camera-sim afterwards" hand-back, updated with what you actually built, in for-tasks.md under "Note for
  the camera-sim session".
