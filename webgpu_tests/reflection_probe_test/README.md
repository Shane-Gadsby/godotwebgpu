# Reflection probe / Octmap test scene

A mirror-metallic sphere in a room of four brightly, distinctly colored emissive
walls, with a `ReflectionProbe` covering it. Exists to exercise the **Octmap**
chain — `CubeToOctmap`, `OctmapDownsampler`, `OctmapFilter`, `OctmapRoughness` —
which is what Godot uses to prefilter reflection probes by roughness, and which is
the only thing that touches the `texture-formats-tier2` storage formats
(`rgb10a2unorm` and friends). See `webgpu_notes/TASKS.md` Task 41.

The per-wall colors are the point: a channel swap or a precision collapse in that
chain shows up as an obviously wrong reflection rather than a subtle one, and the
reflection is the only part of the frame that depends on the chain at all — so the
flat walls and the floor gradient act as built-in controls in the same screenshot.

```bash
<godot_editor> --path webgpu_tests/reflection_probe_test --export-release "Web" builds/index.html
cd webgpu_tests/startup_phases
node shot.mjs <export-dir> /tmp/chrome.png  16000 chromium
node shot.mjs <export-dir> /tmp/firefox.png 16000 firefox
compare -metric MAE /tmp/chrome.png /tmp/firefox.png null:
```

Needs an `export_presets.cfg` with a Web preset; copy one from any project that has
it (the scene itself carries no export configuration).

**Reference result (2026-09-26, NVIDIA Lovelace)**: Chrome takes the native
`rgb10a2unorm` path, Firefox the promoted `rgba16float` one, and the reflections
match — sphere-region mean absolute error 0.0007, against 0.0001 for a floor
gradient and 0.00001 for a flat wall in the same frames.
