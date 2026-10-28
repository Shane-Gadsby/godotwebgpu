#!/usr/bin/env bash
# Exports the async readback project for the web with the editor and the nothreads web templates in bin/.
#   ./export.sh [debug|release]     (default release) -> exports/web/
# Under xvfb-run with Vulkan, not --headless, so the export-time shader baker runs as it does for a user.
# EDITOR_BIN overrides the editor.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TIER="$ROOT/webgpu_tests/async_readback"
PROJ="$TIER/project"
EDITOR="${EDITOR_BIN:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"
KIND="${1:-release}"
[[ -x "$EDITOR" ]] || { echo "no editor at $EDITOR" >&2; exit 1; }
[[ -f "$ROOT/bin/godot.web.template_$KIND.wasm32.nothreads.zip" ]] || { echo "missing bin/godot.web.template_$KIND.wasm32.nothreads.zip" >&2; exit 1; }
"$EDITOR" --headless --path "$PROJ" --import >/dev/null 2>&1 || true
rm -rf "$TIER/exports/web"
mkdir -p "$TIER/exports/web"
xvfb-run -a "$EDITOR" --path "$PROJ" --rendering-driver vulkan --export-$KIND "Web" "$TIER/exports/web/index.html" >"$TIER/exports/export.log" 2>&1
[[ -f "$TIER/exports/web/index.html" ]] || { echo "export produced no index.html (see exports/export.log)" >&2; exit 1; }
echo "exported ($KIND) -> $TIER/exports/web"
