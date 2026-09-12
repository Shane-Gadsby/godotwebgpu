# Live repro methodology: comparing native Vulkan vs WebGPU on a real project

This documents the workflow used throughout Task 9.5 (see `webgpu_notes/TASKS.md`,
Rounds 15 onward) to test the WebGPU driver against a real Godot project — not
the repo's own small synthetic scenes — on both the native Vulkan backend and
the WebGPU export, on real GPU hardware, and to compare their behavior
directly (screenshots, screen recordings, console logs, and raw numeric data
read back out of the engine). Any Claude session reproducing or debugging a
WebGPU-specific visual/behavioral bug should use this rather than reinventing
it.

**This is different from `webgpu_tests/README.md`'s automated suite.** That
suite validates the repo's own fixtures (`shader_corpus`, `test_project`,
`scene_smoketest`, etc.) and is meant to run in CI. This methodology is for
*investigating* a specific reported bug against *arbitrary* project content
(most often a real user project), producing one-off evidence rather than a
pass/fail regression result.

## When to use this

- A bug report describes something *visual* or *behavioral* ("the screen goes
  white", "the lighting looks wrong", "it's dimmer than it should be") rather
  than a console error the shader-corpus/preprocessing tests would catch.
- You need to know whether a symptom is WebGPU-specific or a pre-existing
  upstream Godot behavior — i.e. you need a same-content, same-settings
  comparison against native Vulkan, not just a WebGPU capture in isolation.
- You need actual numbers out of the engine (is a value converging or
  diverging? by how much?), not just a visual impression from a screenshot.

## Prerequisites

- A native editor build: `scons platform=linuxbsd target=editor dev_build=yes -j$(nproc)`
  (or the `platform=macos`/`windows` equivalent).
- A web WebGPU export template: `scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j$(nproc)`
  (see the top-level `CLAUDE.md` for the full command and Emscripten setup).
- The export template actually *installed* where Godot's exporter looks for
  it (`~/.local/share/godot/export_templates/<version>/`) — see the
  freshness-verification section below; a successful build alone is not
  enough.
- A real X11/Wayland display with genuine GPU access (`echo $DISPLAY`,
  `xdpyinfo`) for the native-Vulkan side. `--headless` does **not** work for
  this (see below).
- Playwright installed in this directory for the WebGPU side:
  `npm install playwright && npx playwright install chromium`.

## Step 1 — make a scratch copy of the target project

Never test destructively against the real project directory (especially if
it's the user's own work, outside this repo). Copy it somewhere disposable —
your scratchpad directory, not inside the repo:

```bash
cp -r /path/to/real_project /tmp/scratch/my_repro
rm -rf /tmp/scratch/my_repro/.godot /tmp/scratch/my_repro/.idea   # force a clean re-import
```

Make whatever scene/settings change reproduces the bug directly in the
`.tscn`/`project.godot` text — both are plain text, editable without opening
an editor. For example, to isolate SDFGI specifically:

```
# in main.tscn's [sub_resource type="Environment" ...] block:
sdfgi_enabled = true
```

If you'll export a web build and the project's own `export_presets.cfg` has
`variant/extensions_support=false`, flip it to `true` if you're testing a
`dlink_enabled=yes` engine build — otherwise Godot's exporter silently
resolves to a completely different, non-dlink template and you'll test the
wrong binary without any error (see Round 23 in `webgpu_notes/TASKS.md`).

To keep a long native-Vulkan capture's disk footprint down, you can also
shrink `project.godot`'s `window/size/viewport_width`/`viewport_height` —
see the native-capture section below for why this is the only way to do it.

## Step 2 — native Vulkan capture

### Screenshots / screen recording (frame sequence)

Use `capture_native_vulkan.sh` (this directory):

```bash
./capture_native_vulkan.sh <path/to/godot_editor_binary> /tmp/scratch/my_repro /tmp/scratch/vulkan_frames 300 60
```

This produces `/tmp/scratch/vulkan_frames/frame00000000.png` through
`frame00017999.png` (300s × 60fps) plus an ignorable `.wav`. Sample
`frame%08d.png` at index `seconds*fps - 1` to compare a specific simulated
timestamp against a WebGPU screenshot taken at the same simulated second.

Read the script's header comments before using it blind — in particular:

- **Why not `--headless`**: it forces `--display-driver headless`, which only
  supports the `dummy` (null) rendering driver. No real GPU frames are
  produced at all; a capture "succeeds" but is meaningless.
- **Why `--fixed-fps`/`--disable-vsync`**: decouples simulated time from
  wall-clock speed, so e.g. 300 simulated seconds can finish in well under
  300 real seconds (PNG encoding, not GPU rendering, is typically the actual
  bottleneck). This is what makes it practical to match durations against a
  real-time WebGPU/Playwright capture.
- **Resolution**: the OS `--resolution` flag does *not* control the movie
  capture's internal size — edit the scratch project's
  `project.godot`'s `window/size/viewport_width`/`viewport_height` directly.

### Just watching console output over real time (no screenshots needed)

If you only need `print_line()`-based debug output (see the numeric-readback
section below) rather than frame-accurate visuals, skip movie-capture
entirely and just run the project normally with a wall-clock timeout:

```bash
DISPLAY=:0 timeout 310 <godot_binary> --path /tmp/scratch/my_repro --rendering-driver vulkan > vulkan.log 2>&1
```

## Step 3 — WebGPU capture (Playwright)

First export the scratch project to a fresh web build (see the freshness
section below before trusting anything downstream of this):

```bash
<godot_binary> --path /tmp/scratch/my_repro --headless --export-release "Web" builds/index.html
```

Then use one of the two scripts in this directory, depending on what you
need:

```bash
# All console output (errors, warnings, and any print_line()-based debug
# output) for a fixed real-time duration:
node capture_console.mjs /tmp/scratch/my_repro/builds 300000 > run.log

# A screenshot every interval_ms, count times (e.g. a brightness/visual trend):
node capture_screenshots.mjs /tmp/scratch/my_repro/builds /tmp/scratch/wg_shot 20000 15
```

Both scripts serve the build over a local static HTTP server (with the COOP/
COEP headers required for `dlink`/threaded exports), launch headless Chromium
with flags that force Dawn onto the machine's real Vulkan driver rather than
a silent SwiftShader/software fallback, and either stream console output live
or take timed screenshots. Read each script's header comment for full usage
and the exact Chromium flags used (and why) before adapting them.

**Always confirm the WebGPU capture used real GPU**, not a software fallback
— check the engine's own startup log line naming the adapter (e.g. `WebGPU
1.0 - Forward+ ... NVIDIA ...`), since a sandbox can have both a genuine
Vulkan-backed Chromium and a software-only one depending on its configuration
history, and a software-fallback capture will silently produce misleading
results (usually far slower, sometimes behaviorally different).

## Verifying the export is actually fresh — do this every time

This has repeatedly cost real time across many rounds of Task 9.5: a rebuilt
engine can still silently export a *stale* build if the installed export
template wasn't updated, or if a shader-source edit didn't actually make it
into the compiled binary due to a build-dependency gap. **Before trusting any
capture**, checksum-verify:

```bash
# 1. Make sure the freshly-built template is what gets installed:
cp bin/godot.web.template_release.wasm32.nothreads.dlink.zip \
   ~/.local/share/godot/export_templates/<version>/web_dlink_nothreads_release.zip

# 2. Export, then compare:
md5sum bin/godot.side.web.template_release.wasm32.nothreads.dlink.wasm \
       /tmp/scratch/my_repro/builds/index.side.wasm
```

The two checksums **must match**. If they don't, the export used a stale
template — re-copy and re-export before trusting anything. This is necessary
because Godot's `--export-release` resolves the template from the *installed*
template directory, not directly from your just-built `bin/` output.

If you edited a shader `.glsl` file and want to confirm the fix actually
reached the compiled binary (not just that the build "succeeded"): check that
the generated `<name>.glsl.gen.h` header (under
`servers/rendering/renderer_rd/shaders/.../`) has a newer mtime than your
edit, and that the build log actually shows a `Compiling .../foo.cpp ...`
line for whatever `.cpp` includes it (directly or via a `.h`) — see the
build-staleness traps below for why a "clean" build log can still be lying to
you.

## Known SCons build-staleness traps

Condensed from Task 9.5 Round 30 (`webgpu_notes/TASKS.md` has the full
writeup with exact commands used to diagnose and fix each):

1. **`drivers/webgpu/wgsl_precompiled.gen.h`** (the ahead-of-time SPIR-V→WGSL
   precompile cache) only lists `wgsl_precompile.py` itself as a build
   dependency — not the actual shader `.glsl` sources it precompiles. Editing
   a shader never triggers its regeneration on its own. If your change might
   be covered by its `SHADER_REGISTRY`, delete it manually before rebuilding:
   `rm drivers/webgpu/wgsl_precompiled.gen.h`.
2. Even after that regenerates, the `.cpp` that `#include`s it
   (`drivers/webgpu/rendering_device_driver_webgpu.cpp`) may not get
   recompiled — and even after *that* recompiles, the containing static
   library (`bin/obj/drivers/libdrivers.*.a`) and the final linked
   `.wasm`/`.zip` may not get relinked. If a rebuild produces zero
   `Compiling`/`Linking` lines for files you know changed, delete the
   specific stale `.o`/`.a`/output binary by hand and rebuild again. This is
   the same class of bug as the "three-level cascading SCons staleness" issue
   documented elsewhere in `webgpu_notes/TASKS.md` (search that phrase for
   the module-registration variant of it).
3. **When in doubt, checksum-verify** (previous section) rather than trust a
   "the build succeeded" log.

## Reading numeric data out of the engine (not just visual comparison)

For a purely visual bug, screenshots/movie frames are enough. For a genuinely
numeric question — "is this value converging or diverging, and by how much?"
— add temporary instrumentation directly in the relevant **C++** (there is no
portable printf in compute shaders; never try to do this from GLSL/WGSL
itself), gated by a frame-count throttle so it doesn't spam every frame:

```cpp
static uint64_t s_debug_last_frame = 0;
uint64_t frame = RSG::rasterizer->get_frame_number();
if (frame - s_debug_last_frame >= 60) {
    s_debug_last_frame = frame;
    // ... read + print_line() here ...
}
```

To read a GPU texture's contents back to the CPU for a print like this:
`RD::get_singleton()->texture_get_data(rid, p_layer)`. Two backend-specific
gotchas that will silently produce a misleading result if you don't account
for them:

- **WebGPU's readback is asynchronous.** The first call after a texture
  write returns an empty `Vector` (data not ready yet); only a *later* call
  (any subsequent frame — there's a persistent per-`(texture, layer)`
  staging buffer) returns the actual mapped data. A periodic call (e.g. every
  ~60 frames, as in the snippet above) works fine as an ongoing polling
  pattern; a single one-shot call will not.
- **3D textures are not symmetric between backends.** The generic/Vulkan path
  copies a `TEXTURE_TYPE_3D` texture's *entire* depth in one call, but the
  WebGPU driver's implementation always copies only **one** depth slice
  (`z = p_layer`) — and a 3D texture's layer count is always `1` (layers and
  depth are different fields; only array layers populate the layer count),
  so `p_layer` can only ever be `0`. There is no way to request a different
  depth slice of a 3D texture through this API on WebGPU. Don't rely on this
  for a 3D texture unless you specifically want (and can tolerate not
  knowing what's) at index 0 — a genuine `TEXTURE_TYPE_2D_ARRAY` texture's
  `p_layer` addresses a real, full-plane array layer consistently on **both**
  backends, so prefer instrumenting a 2D-array-shaped resource over a 3D one
  whenever the bug you're chasing touches both.

Decoding raw texel bytes in C++, two common cases:

- Half-float (`RGBA16_SFLOAT` etc.): `Math::half_to_float(uint16_t)`.
- A standard shared-exponent packed format (RGB9E5-style): `scale = pow(2,
  exponent - bias - mantissa_bits)`, then `channel = (raw_bits &
  mantissa_mask) * scale`. **Always verify your decode against the shader's
  own encode function's exact formula** before trusting any printed number —
  bit-shift/mask errors are easy to make and will produce plausible-looking
  but wrong values.

`print_line()` output reaches: native stdout directly; the browser devtools
console (captured by `capture_console.mjs`) at `console.log` (`[log]`) level
— not `[error]`, so a naive error-only filter will miss it.

**Always revert temporary instrumentation before ending the investigation —
it should never be committed.** `git checkout -- <file>` after capturing what
you need, then rebuild once more so `bin/`'s binaries match the clean,
committed source before handing off to the next round/session.

## Files in this directory

| File | Purpose |
|------|---------|
| `capture_console.mjs` | Full console-log capture over a fixed duration (WebGPU side) |
| `capture_screenshots.mjs` | Timed screenshot series (WebGPU side) |
| `capture_native_vulkan.sh` | PNG frame-sequence capture via `--write-movie` (native Vulkan side) |
| `LIVE_REPRO_METHODOLOGY.md` | This document |
| `README.md`, `screenshot_tests.mjs`, `screenshots/` | The repo's own automated small-synthetic-scene regression suite — different purpose, see the top of this doc |

Real per-project test content (the scratch copy itself, its exported build,
any captured frames/logs) is never checked into the repo — it belongs in
your scratchpad directory, since this workflow is inherently about a
specific external project's content, not the repo's own fixtures.
