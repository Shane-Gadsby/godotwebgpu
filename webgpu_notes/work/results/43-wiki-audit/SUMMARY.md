# 43 wiki audit: summary

Wiki commit `823092f` (local, godotwebgpu.wiki master, not pushed). Table and method: AUDIT.md.

- New page Engine-Additions-WebGPU-Export ("Exporting with WebGPU"): setup, browser support, the Forward+ to Mobile
  fallback below 48 sampled textures, differences from native, the GPU readback polling pattern. Linked from Home,
  Engine-Additions and the sidebar.
- Notable Fixes: a WebRTC section (TASKS §4.20: the unreliable-mode typo on every platform, 50 ms default
  `unreliable_lifetime`, web packet size, close, bursts, unset options).
- Shader Baking: the bake-isolation note was a misplaced bullet in "Cache cleanup"; now its own section, plus an
  "Export log" section (§4.27).
- Building: `--no-web-raycast` and the `--skip-tint-cli` alias. Home: install step, double-precision Linux editor,
  macOS first launch.
- Untouched: Camera pages (lead) and the loading-screen section (task 42).

Lead: review `git -C ../godotwebgpu.wiki show 823092f` and push the wiki.
