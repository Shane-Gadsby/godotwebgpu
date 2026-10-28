# 39-camera-followups-user-answers: zebra colors, editor tick boxes for motion blur and handheld shake

State: integrated (lead: merged; tests/run.sh OK (68 checks) on the merged tree; task 38 tests OK)
Owner: worker-39
Claimed: 2026-10-28 09:48
Needs: build, gpu (short renders)
Branch: work/39
Results: webgpu_notes/work/results/39-camera-followups-user-answers/

Read `webgpu_notes/work/camera-batch.md` first. User answers on the hand-offs page (2026-10-28) to questions from
tasks 34, 37 and 38 (all merged: TASKS.md §4.49-4.51):

## Goal

1. **`zebra-colors` -> User-selectable.** Camera3D monitoring: a color property per zebra (zebra 1 and zebra 2),
   defaulting to today's colors (zebra 1 black, zebra 2 white), so the default output is unchanged. Plumb through the
   RS camera monitoring API (`camera_set_monitoring_zebras` or a sibling), RD and Compatibility finish shaders (task
   37's `camera_finish_inc.glsl` and its GLES3 copy), class docs.
2. **`motion-blur-editor` -> "Yes, like DOF and grain", with a tick box to enable it in the editor, default unticked.**
   Motion blur (and rolling shutter with it, the same "while navigating" problem) shows in the editor only when a new
   CameraAttributes property is ticked (e.g. `motion_blur_visible_in_editor`, default false); in a running game the tick
   box has no effect. Implement where the resource sends its settings to the RenderingServer (send "off" under
   `Engine::is_editor_hint()` unless ticked), as task 37 did for Visible in Output. Class docs.
3. **`shake-in-editor` -> "Editor and game", with a tick box to enable it in the editor, default unticked.** Camera3D
   handheld shake moves the editor's camera preview only when a new property is ticked (e.g. `shake_visible_in_editor`,
   default false); the running game always shakes when shake is on. Autofocus is unchanged (it still runs in the
   editor). Class docs.

4. **Handheld shake presets** (user request, 2026-10-28): a `shake_preset` enum on Camera3D whose choices fill out
   every shake setting for you (amplitudes and frequencies per axis, breathing, stabilization, focal-length scaling),
   with **Custom** for hand-tuned values. Picking a preset writes the values; editing any shake value afterwards
   switches the preset to Custom (and the inspector reflects it). A handful of good, distinct presets, e.g. Handheld
   (steady stills grip), Handheld walking, Shoulder rig, Gimbal / stabilizer, Tripod in wind, Vehicle mount,
   Action / running; your call on the exact list. Ground each preset's numbers where possible (physiological hand
   tremor around 8-12 Hz with tiny amplitude, body sway around 0.5-2 Hz, walking cadence around 1.8-2 Hz for vertical
   bob, measured camera-shake studies or IS test standards such as CIPA DC-011) and cite the source in a comment; say
   where a value is a look choice. The default stays as today (preset Custom with today's default values, shake off),
   so existing scenes are unchanged. Document each preset in the class reference and in for-docs.md (a table).

## Verification

Defaults: pixel-identical to the pre-change editor in the three renderers (zebras off), and with zebras on and default
colors identical to before. Zebra colors: the stripe pixels take the chosen colors (numeric check). Tick boxes: in an
editor-hint run (`--editor -s` or a windowed editor under Xvfb, as tasks 37/38 did) motion blur / shake are absent
unticked and present ticked; in a game run they are present either way. Presets: choosing each writes the documented values, and the measured shake (task 38's spectrum/RMS analysis) matches them; editing a value flips the preset to Custom. Task 38's `tests/run.sh` still passes. Smoke.
for-docs.md: the changed paragraphs for the wiki.
