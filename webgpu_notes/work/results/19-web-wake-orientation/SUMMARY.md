# Task 19: web keep-screen-on, orientation lock, performance hint — SUMMARY

State: blocked on the phone (it is on its PIN lock screen; nothing guessed). Code done, desktop check pending the `gpu` lease.

## What changed (work/19, commit cbfdd03209 + this results commit)
- `platform/web/js/libs/library_godot_display.js`: Screen Wake Lock (re-acquired on `visibilitychange`, released at
  exit) and `screen.orientation.lock()` (applied at once and on every fullscreen entry); five new
  `godot_js_display_*` functions (`godot_js.h`). API members are quoted for `use_closure_compiler=yes` (CI web builds).
- `platform/web/display_server_web.{h,cpp}`: `screen_set_keep_on()` / `screen_is_kept_on()`,
  `screen_set_orientation()` / `screen_get_orientation()`, `FEATURE_KEEP_SCREEN_ON` (wake lock API present),
  `FEATURE_ORIENTATION` (lock() present and a coarse pointer), `keep_screen_on` project setting applied at startup.
  Refusals print only with `--verbose`.
- `platform/web/export/export_plugin.{h,cpp}`: new `html/orientation` (default Project Setting); explicit values
  override `display/window/handheld/orientation` in the exported project.binary via `get_custom_project_settings()`;
  the PWA manifest follows the same orientation; `progressive_web_app/orientation` hidden, honoured for old presets;
  manifest `display` CLAMP fixed (0..3, was 0..4 on a 4-entry array).
- `engine.js`: the worker capability probe's `requestAdapter()` also asks for `high-performance` (every other
  request already did: engine.js main device, render thread, webgpu-full-size.html).
- Docs: `doc/classes/DisplayServer.xml`, `platform/web/doc_classes/EditorExportPlatformWeb.xml`.
- Deviation from the task text: the orientation reaches the engine as a project-setting override, not a GODOT_CONFIG
  key — one source of truth inside the engine, works in custom shells, and main.cpp already applies it at startup.

## Verified
- Native editor and web template (release, nothreads, no dlink) build in worktree ../godotwebgpu-19 (`scons` directly,
  same flags as build-linux.sh, 14 GB scope, -j12). I did not run build-linux.sh itself: it installs the editor into
  the shared `/mnt/.../godot-editors/editors` and the `.tpz` into `.../templates`, which would replace the user's
  editor with a worker branch build.
- `pre-commit run --files <changed>`: all passed (eslint reformatted once).
- Driver unit tests: 379 passed, 0 failed.
- smoke.sh: `identical interface: 276/276`, `validation: 276 modules, 0 invalid` — `SMOKE OK (smoke-20261025-171050-2393252)`.
- Exports of the probe project (`wake_proj/`, presets in `export_presets.cfg.example`), baked, 0 export errors:
  manifest orientation Landscape → `landscape-primary`, Portrait → `portrait-primary`, Default (project setting,
  Landscape) → `landscape-primary`, Legacy (only `progressive_web_app/orientation=Portrait`) → `portrait`;
  `handheld/orientation` present in project.binary for Landscape/Portrait/Legacy, absent for Default.

## Not yet verified (what the lead must still run)
1. **Phone** (`phone` lease; the user must unlock it first): export `wake_proj` as Landscape and Portrait (commands
   below), then `node phone_wake_orient.mjs --out <dir> --export Landscape=<exp_Landscape> --export
   Portrait=<exp_Portrait> --wake Landscape --idle 60`. It taps to enter fullscreen through the engine's
   `window_set_mode()`, records `screen.orientation.type` + screencaps, idles 60 s past the 30 s system timeout
   (already 30 s on the phone, `screen_off_timeout=30000`; no setting needs changing), then closes the tab and checks
   the screen goes off. Note the last step lets the phone lock again.
2. **Desktop Chrome + Firefox** (`gpu` lease, briefly): load exp_Default headed; expect `keep_screen_on_feature:
   true` (localhost is secure), `orientation_feature: false`, no new console errors or warnings; tapping toggles
   fullscreen with no console output (the lock rejection is silent).
- Export commands (from the worktree): `xvfb-run -a bin/godot.linuxbsd.editor.x86_64 --rendering-driver vulkan
  --path <wake_proj> --export-release <Landscape|Portrait|Default> <out>/index.html`, presets as in the example
  (the example has my scratch paths; adjust `export_path` and `custom_template/release`).

## Risks
- Default behaviour change: projects that never set `handheld/orientation` (default Landscape) now lock landscape
  in fullscreen on phones, and their PWA manifest says `landscape-primary` instead of `any` (native Android parity).
- A feature-tag override of `handheld/orientation` (e.g. `.web_android`) still beats `html/orientation` at run time.
- The lock only happens in fullscreen; the shells never enter fullscreen by themselves.

## Questions for the user
1. Should the default `html/orientation` be Project Setting (as built; a project left at the default locks landscape
   on phones in fullscreen, like native Android) or Sensor (any rotation, the old web behaviour)? Default: Project Setting.
2. Should the shell enter fullscreen on the first tap on phones (so the orientation lock applies without game code),
   or leave fullscreen to the game? Default: leave it to the game.
