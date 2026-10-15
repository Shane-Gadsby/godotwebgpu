#!/usr/bin/env bash
# Run a Godot scene on native, WebGL, or WebGPU for benchmarking.
#
# Usage:
#   ./run_benchmark.sh native [scene_path]
#   ./run_benchmark.sh webgl  [scene_path]
#   ./run_benchmark.sh webgpu [scene_path]
#
# Environment:
#   BENCH_RENDERER=forward_plus|mobile   scene renderer for the webgpu leg (default forward_plus; the
#                                        scene's own project.godot value is patched for the export and restored).
#                                        Under swiftshader/CI the engine falls back to Mobile on its own.
#   GODOT_WEBGL_BASELINE_ZIP             official non-fork web_nothreads_release.zip for the webgl leg
#   GODOT_WEBGL_BASELINE_EDITOR          the matching official editor binary (a 4.8 pck will not load in a 4.7 template)
#   BENCH_UNCAPPED=1                     opt-in: lift the display/vsync cap so the renderers can be compared (Chrome gets
#                                        --disable-frame-rate-limit --disable-gpu-vsync, native gets --disable-vsync).
#                                        Beyond the project's standard launch flags, hence off by default.
#   BENCH_AUTOQUIT=1                     native leg exits by itself after the results print (for scripted runs)
#
# Default scene: the benchmark project itself (webgpu_tests/benchmark/).
# Pass a path to any Godot project directory to benchmark that instead.
# Injects benchmark_profiler.gd as an autoload to collect CPU/GPU frame times.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PROFILER_GD="$SCRIPT_DIR/benchmark_profiler.gd"

# --- Platform detection ---
# Everything below this block is platform-agnostic; only these four values
# (editor binary, native backend label, in-place-sed invocation, and the
# WebGL-baseline template default) differ between macOS and Linux.
UNAME_S="$(uname -s)"
case "$UNAME_S" in
    Darwin)
        HOST_OS="macos"
        EDITOR_BIN="$REPO_ROOT/bin/godot.macos.editor.arm64"
        NATIVE_BACKEND_LABEL="Metal (Forward+) on macOS"
        # BSD sed requires an explicit (possibly empty) backup-suffix argument.
        sed_inplace() { sed -i '' "$@"; }
        DEFAULT_WEBGL_TEMPLATE_DIR="$HOME/Library/Application Support/Godot/export_templates"
        ;;
    Linux)
        HOST_OS="linux"
        EDITOR_BIN="$REPO_ROOT/bin/godot.linuxbsd.editor.x86_64"
        NATIVE_BACKEND_LABEL="Vulkan (Forward+) on Linux"
        # GNU sed takes the suffix directly appended to -i, with no space.
        sed_inplace() { sed -i "$@"; }
        DEFAULT_WEBGL_TEMPLATE_DIR="$HOME/.local/share/godot/export_templates"
        ;;
    *)
        echo "ERROR: Unsupported platform '$UNAME_S' — this script supports macOS and Linux only."
        exit 1
        ;;
esac

WEBGPU_ZIP="$REPO_ROOT/bin/godot.web.template_release.wasm32.nothreads.zip"
# Use an official (non-fork) WebGL template — this fork's own web build has
# WebGPU loading code baked in even without webgpu=yes, so it can't serve as
# a clean WebGL baseline. There's no reliable way to auto-locate "the official
# one, not this fork's own installed template" across machines, so this is
# always explicit: pass it via GODOT_WEBGL_BASELINE_ZIP, pointing at an
# official (godotengine.org-downloaded) template's web_nothreads_release.zip.
WEBGL_ZIP="${GODOT_WEBGL_BASELINE_ZIP:-}"

MODE="${1:-}"
SCENE_PATH="${2:-$SCRIPT_DIR}"
EXPORT_DIR="$SCRIPT_DIR/exports"
PORT=8099

if [ -z "$MODE" ]; then
    echo "Usage: $0 <native|webgl|webgpu> [scene_path]"
    echo ""
    echo "  native  — Run with $NATIVE_BACKEND_LABEL"
    echo "  webgl   — Export with an official WebGL template, serve in Chrome"
    echo "            (set GODOT_WEBGL_BASELINE_ZIP to a web_nothreads_release.zip"
    echo "            from an official, non-fork Godot export template — this"
    echo "            fork's own web build isn't a clean WebGL baseline)"
    echo "  webgpu  — Export with this fork's WebGPU template, serve in Chrome"
    echo ""
    echo "Default scene: benchmark project (webgpu_tests/benchmark/)"
    exit 1
fi

if [ ! -f "$EDITOR_BIN" ]; then
    echo "ERROR: Editor binary not found: $EDITOR_BIN"
    exit 1
fi

# --- Inject profiler autoload into project.godot ---
inject_profiler() {
    local project_dir="$1"
    local project_file="$project_dir/project.godot"
    local profiler_dest="$project_dir/benchmark_profiler.gd"

    # Copy profiler script into the project (skip if same dir)
    if [ "$(cd "$SCRIPT_DIR" && pwd)" != "$(cd "$project_dir" && pwd)" ]; then
        cp "$PROFILER_GD" "$profiler_dest"
    fi

    # Back up project.godot
    cp "$project_file" "$project_file.bak"

    # Add autoload entry if not already present
    if ! grep -q 'BenchmarkProfiler' "$project_file"; then
        # Append autoload section or add to existing one
        if grep -q '^\[autoload\]' "$project_file"; then
            sed_inplace '/^\[autoload\]/a\
BenchmarkProfiler="*res://benchmark_profiler.gd"
' "$project_file"
        else
            echo '' >> "$project_file"
            echo '[autoload]' >> "$project_file"
            echo 'BenchmarkProfiler="*res://benchmark_profiler.gd"' >> "$project_file"
        fi
    fi
}

# --- Restore project.godot after benchmarking ---
restore_project() {
    local project_dir="$1"
    local project_file="$project_dir/project.godot"
    local profiler_dest="$project_dir/benchmark_profiler.gd"

    if [ -f "$project_file.bak" ]; then
        mv "$project_file.bak" "$project_file"
    fi
    # Only remove profiler if we copied it in (also its Godot 4.x .uid sidecar)
    if [ "$(cd "$SCRIPT_DIR" && pwd)" != "$(cd "$project_dir" && pwd)" ]; then
        rm -f "$profiler_dest" "$profiler_dest.uid"
    fi
}

# Set the web renderer of a project (the .bak taken by inject_profiler restores it afterwards).
set_web_renderer() {
    sed_inplace "s|^renderer/rendering_method.web=.*|renderer/rendering_method.web=\"$2\"|" "$1/project.godot"
    grep -q '^renderer/rendering_method.web=' "$1/project.godot" || \
        sed_inplace "s|^\[rendering\]|[rendering]\nrenderer/rendering_method.web=\"$2\"|" "$1/project.godot"
}

# Resolve scene path to absolute
SCENE_PATH="$(cd "$SCENE_PATH" && pwd)"

# --- Native ---
if [ "$MODE" = "native" ]; then
    echo "=== Native ($NATIVE_BACKEND_LABEL) ==="
    echo "Scene: $SCENE_PATH"
    inject_profiler "$SCENE_PATH"
    trap "restore_project '$SCENE_PATH'" EXIT
    if [ "${BENCH_AUTOQUIT:-0}" = "1" ]; then
        # 180 warmup + 300 collect frames, plus headroom for startup and a vsync-capped display.
        "$EDITOR_BIN" --rendering-driver vulkan ${BENCH_UNCAPPED:+--disable-vsync} --path "$SCENE_PATH" --quit-after 1500
    else
        "$EDITOR_BIN" --path "$SCENE_PATH"
    fi
    exit 0
fi

# --- Web (WebGL or WebGPU) ---
if [ "$MODE" = "webgl" ]; then
    TEMPLATE_ZIP="$WEBGL_ZIP"
    PRESET="WebGPU"   # the projects carry one preset; the template and renderer are what differ
    WEB_RENDERER="gl_compatibility"
    echo "=== WebGL on Chrome ==="
elif [ "$MODE" = "webgpu" ]; then
    TEMPLATE_ZIP="$WEBGPU_ZIP"
    PRESET="WebGPU"
    WEB_RENDERER="${BENCH_RENDERER:-forward_plus}"
    echo "=== WebGPU (${WEB_RENDERER}) on Chrome ==="
else
    echo "ERROR: Unknown mode '$MODE'. Use native, webgl, or webgpu."
    exit 1
fi

if [ "$MODE" = "webgl" ] && [ -z "$TEMPLATE_ZIP" ]; then
    echo "ERROR: GODOT_WEBGL_BASELINE_ZIP is not set."
    echo "Point it at an official (non-fork) Godot export template's web_nothreads_release.zip"
    echo "— this fork's own web build always has WebGPU loading code baked in, so it can't"
    echo "serve as a clean WebGL baseline. Official templates are typically under:"
    echo "  $DEFAULT_WEBGL_TEMPLATE_DIR/<version>.stable[.official.<hash>]/web_nothreads_release.zip"
    exit 1
fi

if [ ! -f "$TEMPLATE_ZIP" ]; then
    echo "ERROR: Template not found: $TEMPLATE_ZIP"
    echo "Build it with: scons platform=web target=template_release threads=no use_assertions=no [webgpu=yes]"
    exit 1
fi

SCENE_NAME="$(basename "$SCENE_PATH")"
if [ "$MODE" = "webgpu" ]; then
    SCENE_EXPORT_DIR="$EXPORT_DIR/${SCENE_NAME}_${MODE}_${WEB_RENDERER}"
else
    SCENE_EXPORT_DIR="$EXPORT_DIR/${SCENE_NAME}_${MODE}"
fi
EXPORT_HTML="$SCENE_EXPORT_DIR/index.html"

echo "Scene: $SCENE_PATH"
echo "Template: $TEMPLATE_ZIP"
echo "Export to: $SCENE_EXPORT_DIR"

# Patch export_presets.cfg to use the correct template
PRESETS_CFG="$SCENE_PATH/export_presets.cfg"
if [ ! -f "$PRESETS_CFG" ]; then
    echo "ERROR: No export_presets.cfg in $SCENE_PATH"
    exit 1
fi

# Inject profiler and back up project files
inject_profiler "$SCENE_PATH"
cp "$PRESETS_CFG" "$PRESETS_CFG.bak"
set_web_renderer "$SCENE_PATH" "$WEB_RENDERER"
sed_inplace "s|custom_template/release=\"[^\"]*\"|custom_template/release=\"$TEMPLATE_ZIP\"|g" "$PRESETS_CFG"

# Ensure cleanup on exit
trap "restore_project '$SCENE_PATH'; [ -f '$PRESETS_CFG.bak' ] && mv '$PRESETS_CFG.bak' '$PRESETS_CFG'" EXIT

# Export
mkdir -p "$SCENE_EXPORT_DIR"
echo "Exporting..."
# The WebGL leg needs an official (non-fork) editor too: a pck made by this fork's 4.8 editor is "newer"
# than the official 4.7.x template and refuses to load. Set GODOT_WEBGL_BASELINE_EDITOR to that editor.
EXPORT_EDITOR="$EDITOR_BIN"
[ "$MODE" = "webgl" ] && [ -n "${GODOT_WEBGL_BASELINE_EDITOR:-}" ] && EXPORT_EDITOR="$GODOT_WEBGL_BASELINE_EDITOR"
"$EXPORT_EDITOR" --headless --path "$SCENE_PATH" --export-release "$PRESET" "$EXPORT_HTML" 2>&1 | tail -5

if [ ! -f "$EXPORT_HTML" ]; then
    echo "ERROR: Export failed — $EXPORT_HTML not created"
    exit 1
fi
echo "Export complete."

# Serve and launch Chrome via Playwright (captures console output)
echo "Launching Chrome via Playwright with console capture..."

# Kill any existing server on this port
lsof -ti:$PORT 2>/dev/null | xargs kill -9 2>/dev/null || true

PW_NODE_MODULES="$SCRIPT_DIR/../scene_smoketest/node_modules"
node -e "
const http = require('http');
const fs = require('fs');
const path = require('path');
const pw = require(path.resolve('$PW_NODE_MODULES', '..', 'node_modules', 'playwright'));

const MIME = {'.html':'text/html','.js':'text/javascript','.wasm':'application/wasm','.pck':'application/octet-stream','.png':'image/png','.json':'application/json','.svg':'image/svg+xml','.ico':'image/x-icon'};
const dir = '$SCENE_EXPORT_DIR';

const server = http.createServer((req, res) => {
    let p = path.join(dir, req.url === '/' ? 'index.html' : req.url);
    if (!fs.existsSync(p)) { res.writeHead(404); return res.end('Not found'); }
    res.setHeader('Content-Type', MIME[path.extname(p)] || 'application/octet-stream');
    res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
    res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
    res.end(fs.readFileSync(p));
});

server.listen($PORT, async () => {
    console.log('Server ready on port $PORT');
    // System Chrome with the project's standard launcher flags (scene_smoketest/run_scenes.mjs).
    const browser = await pw.chromium.launch({
        headless: false,
        ...(process.platform === 'linux' ? { executablePath: '/usr/bin/google-chrome-stable' } : {}),
        ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
        args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist',
               ...(process.env.BENCH_UNCAPPED ? ['--disable-frame-rate-limit', '--disable-gpu-vsync'] : [])],
    });
    const page = await browser.newPage();

    // Forward all console messages to stdout.
    page.on('console', msg => {
        const text = msg.text();
        console.log(text);
    });
    page.on('pageerror', err => console.log('[PAGE-ERROR] ' + err.message));

    await page.goto('http://127.0.0.1:$PORT');
    console.log('Chrome opened — waiting for benchmark results...');

    // Wait until we see the final benchmark line or timeout after 60s.
    await Promise.race([
        page.waitForEvent('console', { predicate: msg => msg.text().includes('================================================'), timeout: 60000 }),
        new Promise(r => setTimeout(r, 60000)),
    ]);

    // Give a moment for any trailing output.
    await new Promise(r => setTimeout(r, 1000));
    await browser.close();
    server.close();
});
"
