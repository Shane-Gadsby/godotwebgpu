# WebGPU debug probe

A standalone script for tracking down real-hardware-only WebGPU validation
errors that the project's sandbox can't reproduce — see
`webgpu_notes/TASKS.md` Task 9.5 Round 9/10 for the specific error this was
built for (a `TextureViewDimension ... not compatible with ... TextureDimension`
error that only ever shows up on real hardware).

It wraps `GPUDevice.createTexture` and `GPUTexture.createView` so every call
is logged with its real arguments and a JS stack trace, *before* the
browser's own async validation runs — so even when Dawn's error message is
vague (a texture with no debug label shows up as `"unnamed#15"`), this probe
has the ground truth captured directly at the call site.

## Usage

1. Copy `wgpu_debug_probe.js` into your exported web build's output folder
   (next to `index.html`).
2. Open `index.html` in a text editor and add this line as the **very
   first** line inside `<head>`, before any other `<script>` tag:
   ```html
   <script src="wgpu_debug_probe.js"></script>
   ```
   It must load before Godot's own `index.js`, so the prototypes are patched
   before the engine ever creates a device or texture.
3. Serve the folder (e.g. `python3 -m http.server 8642`) and open it in
   Chrome with DevTools open.
4. Let the scene run as normal. If you're using the `ShaderCoverage` test
   scene, let it run through "Rendering 10 frames..." and leave it another
   10-20 seconds past that to be safe.
5. In the DevTools console, run:
   ```js
   window.__wgpuProbeDump()        // human-readable summary
   copy(window.__wgpuProbeExport()) // copies the full JSON log to your clipboard
   ```
6. Share the dump output (or the copied JSON) back.

Note: this edit has to be redone each time you re-export, since it modifies
the exported `index.html` directly, not the project source.

## What to look for

- Every 3D-dimension texture created, with its real size/format/label and a
  stack trace from the moment it was created.
- Any `createView()` call whose requested view dimension is structurally
  incompatible with the real texture's own dimension (the same check this
  driver's C++ side already applies as a fallback substitution) — logged in
  bold red, with a full stack trace pointing at the exact call site.
