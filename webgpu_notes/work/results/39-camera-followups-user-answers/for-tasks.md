# For TASKS.md (task 39)

## §4 entry (next number after the camera batch's)

**Camera follow-ups from the hand-offs page (task 39, 2026-10-28).** The user's answers to the questions of tasks 34,
37 and 38, plus shake presets (user request the same day):

- **Zebra colors**: Camera3D `monitoring_zebras_color` (default black) and `monitoring_zebras_second_color` (default
  white), display colors. RS `camera_set_monitoring_zebra_colors(camera, color, second_color)` (a sibling of
  `camera_set_monitoring_zebras`, so that method's signature is unchanged), `CameraMonitoring::zebras_color /
  zebras_second_color`. RD: two `vec4` at the end of the camera finish push constant, which grows from 96 to 128 bytes
  (the Vulkan minimum `maxPushConstantsSize`, now full: `static_assert` in `camera_finish.h`; task 37's comment said 112,
  it was 96). Compatibility: two `vec3` uniforms. No interface-reference change (push-constant members are not in it).
- **Motion blur / rolling shutter in the editor**: CameraAttributes `motion_blur_visible_in_editor` and
  `rolling_shutter_visible_in_editor` (default off, one per group because each group's enable check box hides its
  other properties). Under `Engine::is_editor_hint()` the resource sends the effect as off unless ticked
  (`CameraAttributes::_camera_motion_shows()`); a running project always gets it. Same place and pattern as Camera3D's
  Visible in Output.
- **Handheld shake in the editor**: Camera3D `shake_visible_in_editor` (default off). `_shake_active()` = enabled and
  (ticked or not the editor) gates the internal process, the offset in `_get_adjusted_camera_transform()` and
  `get_shake_offset()`, so an unticked camera does no shake work in the editor. Autofocus unchanged.
- **Shake presets**: Camera3D `shake_preset` (enum `ShakePreset`: Custom, Handheld, Handheld Stabilized, Handheld
  Walking, Running, Shoulder Rig, Gimbal, Tripod in Wind, Vehicle Mount). Choosing one writes every shake setting except
  the seed (table + sources in the `/* Handheld shake presets */` comment in `camera_3d.cpp`); any shake setter that
  leaves the values different from the preset's flips it to Custom (`_shake_value_changed()`), and a value equal to the
  preset's keeps it, which is what lets a saved scene (preset property first, then its values) load with its preset.
  Exact float comparison on purpose: saved floats read back bit-exact. Default Custom with task 38's values, shake off.

Tests: `webgpu_notes/work/results/39-camera-followups-user-answers/tests/run.sh <editor> [scratch]` (BASE=<pre-change
editor> adds pixel identity).

## §3 trap candidate

- **A test that asserts a default behavior breaks when the user changes the default**: task 38's editor check
  (`tests/edproj`) asserts the shake moves in the editor; since task 39 that needs `shake_visible_in_editor = true` in
  its `main.tscn`. My `verify.sh` runs it on a copy with that line added. The lead should add the line to task 38's
  `tests/edproj/main.tscn` when integrating (I did not write into another task's results folder).

## Questions for the user

None blocking. Defaults chosen:
- Two editor tick boxes (one in Motion Blur, one in Rolling Shutter) rather than one shared box: each group's enable
  check box hides the rest of its group, so one shared box would be unreachable whenever only the other effect is on.
- Preset list: the eight above. Stabilization appears only in Handheld Stabilized; Gimbal is modeled with small
  rotation amplitudes rather than the stabilization slider, because the slider also damps translation, which a gimbal
  does not remove.
