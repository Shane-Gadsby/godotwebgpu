# Multi-Browser Screenshot Comparison

Automated visual regression testing for the WebGPU rendering backend. Captures screenshots of deterministic WebGPU scenes across Chrome and Firefox, then compares against baselines.

**Investigating a specific reported bug against a real project (not this suite's own fixtures)?** See [`LIVE_REPRO_METHODOLOGY.md`](LIVE_REPRO_METHODOLOGY.md) instead — it documents the scratch-copy + native-Vulkan-vs-WebGPU comparison workflow used throughout Task 9.5, including the reusable `capture_console.mjs`/`capture_screenshots.mjs`/`capture_native_vulkan.sh` scripts.

## Test Scenes

| Scene | What it exercises |
|-------|------------------|
| **triangle** | Vertex colors, basic rasterization, clear color |
| **textured_quad** | Texture upload, sampler creation, UV mapping |
| **instanced** | Instance buffers, vertex attribute layouts, 64 draw instances |
| **compute_pattern** | Compute→render pipeline, storage textures, Mandelbrot fractal |

## Running

### First run (create baselines)
```bash
npm install playwright
npx playwright install chromium firefox
node screenshot_tests.mjs --update-baselines
```

### Subsequent runs (compare against baselines)
```bash
node screenshot_tests.mjs
```

### Options
```
--update-baselines    Save current screenshots as new baselines
--threshold 0.05      Fraction of pixels allowed to differ (0-1, default 0.01)
--pixel-tolerance 16  Per-channel 0-255 delta below which two pixels count as
                      the same (default 8)
```

## Output

```
screenshots/
├── baselines/          Reference images (committed to git)
│   ├── chromium_triangle.png
│   ├── chromium_textured_quad.png
│   ├── firefox_triangle.png
│   └── ...
├── current/            Latest captures (gitignored)
├── diffs/              Visual diff images on failure (gitignored)
└── report.json         Machine-readable results
```

## What it catches

- **Regression within a browser** — shader compilation changes, resource binding errors, format promotion bugs
- **Cross-browser divergence** — implementation differences between Chrome's Dawn and Firefox's wgpu backends
- **Driver updates** — GPU driver changes that alter rasterization behavior

## Comparison approach

PNGs are decoded to RGBA8 (`png.mjs`, zlib only — no dependencies) and compared
per pixel. A pixel counts as different when any channel differs by more than
`--pixel-tolerance`; the test fails when more than `--threshold` of the pixels
do. The tolerance exists because GPU rasterization differs slightly between
machines and drivers — gradient dithering and edge coverage move pixels by one
or two levels, while a real rendering change moves whole regions far further.

- **Same-browser regression**: threshold 1% of pixels
- **Cross-browser comparison**: looser threshold (5x), reported as a warning only, since implementations legitimately differ in edge-case rasterization

Do **not** compare the raw PNG bytes: deflate output is not locally stable, so a
single changed pixel rewrites most of the stream and reads as a ~99% difference
between images that look identical. That is what this script used to do, and it
failed every run on any machine but the one that produced the baselines.

A capture that comes back entirely pure black is recorded as a skip, not a
failure: it means the browser never composited (Firefox under Xvfb on a GPU-less
runner does this), and these scenes are hand-written WebGPU JS that never touch
the Godot driver, so nothing in this repo can turn one black. If *every* browser
skips, the run fails rather than passing on nothing.

## CI integration

Add `?autorun` query param or use the Playwright runner directly. The test exits with code 1 on failure for CI integration.
