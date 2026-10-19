#!/usr/bin/env bash
# Exports fixture_project to exports/nothreads and exports/threads with the editor and web templates in bin/.
# EDITOR_BIN overrides the editor. The threaded export is skipped when its template is absent.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TIER="$ROOT/webgpu_tests/web_audio"
PROJ="$TIER/fixture_project"
EDITOR="${EDITOR_BIN:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"

[[ -x "$EDITOR" ]] || { echo "no editor at $EDITOR -- build one or set EDITOR_BIN" >&2; exit 1; }
[[ -f "$ROOT/bin/godot.web.template_release.wasm32.nothreads.zip" ]] || { echo "missing template bin/godot.web.template_release.wasm32.nothreads.zip" >&2; exit 1; }

"$EDITOR" --headless --path "$PROJ" --import >/dev/null 2>&1 || true
mkdir -p "$TIER/exports/nothreads" "$TIER/exports/threads"

rc=0
echo "exporting nothreads -> $TIER/exports/nothreads"
"$EDITOR" --headless --path "$PROJ" --export-release "Web" "$TIER/exports/nothreads/index.html" >/dev/null 2>&1 || rc=1
[[ -f "$TIER/exports/nothreads/index.html" ]] || { echo "nothreads export produced no index.html" >&2; rc=1; }

if [[ -f "$ROOT/bin/godot.web.template_release.wasm32.zip" ]]; then
	echo "exporting threads -> $TIER/exports/threads"
	"$EDITOR" --headless --path "$PROJ" --export-release "Web-Threads" "$TIER/exports/threads/index.html" >/dev/null 2>&1 || rc=1
	[[ -f "$TIER/exports/threads/index.html" ]] || { echo "threads export produced no index.html" >&2; rc=1; }
else
	echo "skipping threaded export (no bin/godot.web.template_release.wasm32.zip)"
fi
exit $rc
