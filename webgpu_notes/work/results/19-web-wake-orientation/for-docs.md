# Task 19: text for the shared docs

## TASKS.md §2.36 (status line + what was done; move to §4 once the phone check passes)

**Done 2026-10-25 (work/19):**
- *Screen awake:* `DisplayServerWeb::screen_set_keep_on()` requests a Screen Wake Lock
  (`navigator.wakeLock.request('screen')`) and asks again on every `visibilitychange` to visible (the browser drops
  the lock whenever the page is hidden). The constructor applies `display/window/energy_saving/keep_screen_on`, as
  X11/Wayland/Windows do. `FEATURE_KEEP_SCREEN_ON` is true where `navigator.wakeLock` exists (absent in insecure
  contexts). The lock is released at engine exit (`GodotOS.atexit`).
- *Orientation:* `screen_set_orientation()` / `screen_get_orientation()` implemented: the ScreenOrientation maps to
  `screen.orientation.lock()` (`landscape-primary`, `portrait-primary`, `-secondary`, `landscape`, `portrait`; Sensor
  unlocks). The lock is requested at once and again on every `fullscreenchange` into fullscreen (any element), since
  Chrome for Android only allows it in fullscreen and drops it on exit. `FEATURE_ORIENTATION` is true only where
  `lock()` exists **and** `(any-pointer: coarse)` matches: desktop browsers have `lock()` but always reject it, and
  main.cpp only calls `screen_set_orientation()` at startup when the feature is reported.
- *Export option:* `html/orientation` (Project Setting / Landscape / Portrait / Reverse Landscape / Reverse Portrait /
  Sensor Landscape / Sensor Portrait / Sensor), default Project Setting. Anything else is written into the exported
  `project.binary` as `display/window/handheld/orientation` (`get_custom_project_settings()`, as iOS does), so the
  engine applies it — no GODOT_CONFIG key, works with custom shells. The PWA manifest follows the same resolved
  orientation; `progressive_web_app/orientation` is hidden and only honoured for old presets (Landscape/Portrait →
  Sensor Landscape/Portrait) while `html/orientation` is Project Setting. The manifest's `display` CLAMP read past
  its 4-entry array (clamped to 4) and is fixed (0..3).
- *Performance:* every adapter request already asked for `powerPreference: 'high-performance'` except the
  worker capability probe in `engine.js` (`probeWebGPUWorker`), which now does too. The web has no stronger signal:
  `powerPreference` is a hint (dual-GPU laptops, some phones), the Compute Pressure API only observes.
- All refusals (no API, hidden page, lock outside fullscreen, desktop) are silent unless `--verbose`, where they are
  printed with the browser's reason (`GodotRuntime.print`), never `console.error`/`warn`.

**Behaviour change to note:** a project that never set `display/window/handheld/orientation` gets the default,
Landscape, so on a phone the export now locks to landscape when it enters fullscreen, and an installed PWA's manifest
says `landscape-primary` instead of `any`. That is what native Android does with the same project; set the project
setting (or `html/orientation`) to Sensor for the old free rotation.

**Fullscreen is the game's job:** the lock only applies in fullscreen, and nothing in the shells enters fullscreen on
its own; a project calls `DisplayServer.window_set_mode(WINDOW_MODE_FULLSCREEN)` from an input event.

**Known edge:** a feature-tag override of the setting (`display/window/handheld/orientation.web_android`, say) still
beats `html/orientation` at run time, because the export override writes the base key only.

## webgpu_site/CORRECTNESS_AND_COMPATIBILITY.md (web platform features)

Web exports keep the screen on (Screen Wake Lock, `display/window/energy_saving/keep_screen_on`, secure context
required) and lock the screen orientation chosen by the `html/orientation` export option (default: the
`display/window/handheld/orientation` project setting) whenever the page is in fullscreen on a phone or tablet;
desktop browsers cannot lock orientation and report `FEATURE_ORIENTATION` as unsupported.
