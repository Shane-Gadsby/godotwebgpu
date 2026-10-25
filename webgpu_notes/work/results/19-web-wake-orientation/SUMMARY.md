# Task 19: web keep-screen-on, orientation lock, fullscreen on first tap, performance hint — SUMMARY

State: ready-for-integration (branch work/19; worktree ../godotwebgpu-19)

## What changed
- `platform/web/js/libs/library_godot_display.js` (+ five `godot_js_display_*` in `godot_js.h`): Screen Wake Lock
  (re-requested on `visibilitychange` to visible, released at exit); `screen.orientation.lock()` (requested at once
  and on every fullscreen entry, any element); fullscreen on the first tap (`touchend` on the canvas, primary pointer
  coarse only, stops after the first successful entry). API members quoted for `use_closure_compiler=yes` (CI web
  builds). Refusals print only with `--verbose` (`GodotRuntime.print`), never console.error/warn.
- `platform/web/display_server_web.{h,cpp}`: `screen_set_keep_on()` / `screen_is_kept_on()`, `screen_set_orientation()`
  / `screen_get_orientation()`; `FEATURE_KEEP_SCREEN_ON` = `navigator.wakeLock` exists; `FEATURE_ORIENTATION` =
  `lock()` exists and `(any-pointer: coarse)` (desktops have `lock()` but always reject it); `keep_screen_on` applied
  at startup as on X11/Wayland/Windows.
- `platform/web/export/export_plugin.{h,cpp}`: `html/orientation` (Project Setting / the 7 ScreenOrientations; default
  Project Setting) — explicit values override `display/window/handheld/orientation` in the exported project.binary
  (`get_custom_project_settings()`, as iOS does), which main.cpp applies at startup; the PWA manifest follows the same
  orientation; `progressive_web_app/orientation` hidden, honoured for old presets. `html/fullscreen_on_first_tap`
  (default on) → `fullscreenOnFirstTap` (config.js + GodotConfig; Engine API default off so hand-written shells are
  unchanged). Manifest `display` CLAMP read past its 4-entry array: fixed.
- `engine.js`: the worker capability probe's `requestAdapter()` also asks for `high-performance`; every other adapter
  request already did (main device, render thread, webgpu-full-size.html). The web has no stronger signal.
- Docs: `doc/classes/DisplayServer.xml`, `platform/web/doc_classes/EditorExportPlatformWeb.xml`. Shared-doc text and a
  new §3 trap in `for-docs.md`.
- Deviation from the task text: the orientation reaches the engine as a project-setting override, not a GODOT_CONFIG
  key (one source of truth, works with custom shells).

## Verified
- Native editor + web template (release, nothreads, no dlink) build in the worktree (`scons` directly with
  build-linux.sh's flags, 14 GB scope, -j12 — not build-linux.sh itself, which installs into the shared
  `godot-editors/editors` and `templates` dirs, i.e. the user's editor).
- `pre-commit` on all changed files: pass. Driver unit tests: 379 passed, 0 failed.
- smoke.sh on the final tree: `identical interface: 276/276`, `validation: 276 modules, 0 invalid, 0 of them expected`,
  `SMOKE OK (smoke-20261025-175347-418425)`.
- Exports of the probe project (`wake_proj/`, presets in `wake_proj/export_presets.cfg.example`), baked, 0 export
  errors. Manifest orientation: Landscape → `landscape-primary`, Portrait → `portrait-primary`, Default (project
  setting Landscape) → `landscape-primary`, Legacy (only `progressive_web_app/orientation=Portrait`) → `portrait`;
  override in project.binary for Landscape/Portrait/Legacy, absent for Default.
- **Phone** (Razr 60, Chrome 155; `phone_wake_orient.mjs`, `phone_run/report.json` + screencaps):
  - probe: `keep_screen_on_feature` true, `orientation_feature` true, `kept_on` true, `orientation` 0 / 1 as exported.
  - first tap: Portrait export → fullscreen, `portrait-primary` 411x964; Landscape export → fullscreen,
    `landscape-primary` 964x411 (display rotated, `Landscape_fullscreen.png`). Second tap stays fullscreen.
  - `html/fullscreen_on_first_tap=false`: taps leave it windowed, portrait.
  - wake lock: 60 s untouched against `screen_off_timeout=30000`: Awake at every 5 s sample; tab closed → Awake 25 s,
    Dozing at 30 s. No phone setting changed (timeout was already 30 s).
  - console: only the known `float32-blendable` notice and Chrome's AudioContext autoplay notice; nothing from this.
  - An earlier run (window_set_mode() from the probe's own tap) showed the engine path also locks, provided the request
    is made on touch release (§3 trap text in for-docs.md).
- Desktop Chrome + Firefox: see "Desktop" below.

## Desktop
PENDING (gpu lease)

## Risks
- Default behaviour changes (both as the user decided 2026-10-25): phones now go fullscreen on the first tap, and
  projects left at the default `handheld/orientation` (Landscape) then lock landscape, like native Android; their PWA
  manifest says `landscape-primary` instead of `any`. `html/fullscreen_on_first_tap` off / orientation Sensor restore
  the old behaviour.
- A feature-tag override of `handheld/orientation` (e.g. `.web_android`) still beats `html/orientation` at run time.
- The first-tap listener is on the canvas: a shell overlay that swallows touches before the canvas would stop it (the
  stock shells have none once loaded).

## Questions for the user
None open (answered 2026-10-25: keep Project Setting as the html/orientation default; fullscreen on the first tap on
phones, on by default, with an export option to turn it off).
