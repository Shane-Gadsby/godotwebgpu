# 19-web-wake-orientation: web export keeps the screen awake, asks for performance, locks orientation

State: claimed
Owner: worker-19
Claimed: 2026-10-25 17:03
Needs: phone (Razr 60), build, gpu only for desktop checks
Branch: work/19
Results: webgpu_notes/work/results/19-web-wake-orientation/

## Context

User request, 2026-10-25. The full write-up, with what exists today and the plan, is webgpu_notes/TASKS.md §2.36; read it
first. Not part of the converter swap, run on the same board.

## Goal

1. `screen_set_keep_on()` on the web with the Screen Wake Lock API, re-acquired on `visibilitychange`, applied from
   `display/window/energy_saving/keep_screen_on` at startup, `FEATURE_KEEP_SCREEN_ON` reported where the API exists.
2. A non-PWA orientation export option (default: follow `display/window/handheld/orientation`) passed through
   `GODOT_CONFIG`; the shell locks `screen.orientation` on entering fullscreen; `screen_set_orientation()` /
   `FEATURE_ORIENTATION` on the same code. One place to set it (the PWA manifest follows the new option or vice versa);
   fix the `CLAMP` out-of-range bug in `export_plugin.cpp`'s manifest code.
3. `powerPreference: 'high-performance'` on every adapter request (shells in `misc/dist/html/`, render thread, editor).

Where an API is missing or rejects (desktop orientation lock, older browsers), fail quietly with `print_verbose`, never
an error in the console (CLAUDE.md "On web, pick the severity deliberately").

## Verification

- Razr 60, Chrome (TASKS.md §2.9 recipe, `phone` lease): the screen stays on past the system timeout while the export
  runs and dims normally after the tab is closed; in fullscreen the export locks to the chosen orientation, for both
  Portrait and Landscape exports. Screencaps in results.
- Desktop Chrome and Firefox (Linux, real GPU): no new console errors or warnings; `has_feature` answers match what the
  browser supports.
- Native editor builds; web template builds; `pre-commit run --files <changed>`; driver unit tests unaffected.
