# 69-preload-text-opacity-font

State: ready-for-integration
Owner: worker-69
Claimed: 2026-10-28
Needs: build, browser
Branch: work/69
Results: webgpu_notes/work/results/69-preload-text-opacity-font/

User answers on task 61's questions (hand-offs page, 2026-10-28): `preload-text-opacity` **"Full opacity"** and
`preload-text-font` **"Add font and size"**. Read TASKS §4.77 and `results/61-preload-text-options/` first.

1. The start screen's download line (`html/start_button/preload_text/*`) is drawn at full opacity, so `color` shows
   exactly as picked. Keep the default start screen looking as it does today: pick the default `color` so that at full
   opacity it renders the same as `#e0e0e0` at 85% did over the default background (say what you chose and measure
   it), and keep old presets (with an explicit `#e0e0e0`) looking as the user set it, i.e. now exact.
2. New options `html/start_button/preload_text/font_family` (default: the shell's current font) and `font_size`
   (default: the current size), shown only while `preload_progress` is on, passed in `GODOT_CONFIG.startButton.preloadText`,
   applied in both default shells; documented in `config.js` and the class reference; custom-shell docs updated.

Verification: defaults pixel-identical to today's start screen in Chrome and Firefox (task 61's `preload_text_test.mjs`
and its compare); a chosen colour reads back exactly; a font family and size apply; option visibility.

## Rules (lead)

- Read `webgpu_notes/work/README.md` (browser launch rule, release-template rule) and CLAUDE.md first.
- **Test scope (user, 2026-10-28):** only the tests directly related to this task, the default/bit-identical check, the
  dev_mode editor build and the web `template_release` compile. No full Forward+ matrix, scene smoketest or perf_guard;
  smoke.sh only if a shader interface changes. Say in SUMMARY.md what you skipped.
- camera-sim (`/mnt/109313D2109313D2/godot-game-dev/camera-sim`) is read-only; scratch copies only.
