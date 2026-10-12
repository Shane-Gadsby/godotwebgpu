#!/usr/bin/env bash
# Exports the congestion fixture project for the web, so a browser peer can sit
# on one side of shape.sh's link.
#
#   ./export.sh              # nothreads -> exports/congestion
#   RTC_THREADS=yes ./export.sh   # also threads -> exports/congestion_threads
#
# EDITOR_BIN overrides the editor. The presets point at ../../../../bin/*.zip
# relative to the project, so no per-machine path fixing is needed.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TIER="$ROOT/webgpu_tests/webrtc_congestion"
PROJ="$TIER/fixtures/congestion_project"
EDITOR="${EDITOR_BIN:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"

if [[ ! -x "$EDITOR" ]]; then
	echo "no editor at $EDITOR — build one or set EDITOR_BIN" >&2
	exit 1
fi
if [[ ! -f "$ROOT/bin/godot.web.template_release.wasm32.nothreads.zip" ]]; then
	echo "missing bin/godot.web.template_release.wasm32.nothreads.zip" >&2
	exit 1
fi

mkdir -p "$TIER/exports/congestion" "$TIER/exports/congestion_threads"

# First scan separately and ignoring the exit code: with the webrtc-native
# reference extension present the editor aborts (exit 134) the first time it
# scans a project (F6, upstream). It still writes .godot/extension_list.cfg.
"$EDITOR" --headless --path "$PROJ" --import >/dev/null 2>&1 || true

rc=0
echo "exporting nothreads -> $TIER/exports/congestion"
"$EDITOR" --headless --path "$PROJ" \
	--export-release "WebGPU" "$TIER/exports/congestion/index.html" >/dev/null 2>&1 || rc=1
if [[ ! -f "$TIER/exports/congestion/index.html" ]]; then
	echo "nothreads export produced no index.html" >&2
	rc=1
fi

if [[ "${RTC_THREADS:-no}" == "yes" ]] && [[ -f "$ROOT/bin/godot.web.template_release.wasm32.zip" ]]; then
	echo "exporting threads -> $TIER/exports/congestion_threads"
	"$EDITOR" --headless --path "$PROJ" \
		--export-release "WebGPU-Threads" "$TIER/exports/congestion_threads/index.html" >/dev/null 2>&1 || rc=1
	if [[ ! -f "$TIER/exports/congestion_threads/index.html" ]]; then
		echo "threads export produced no index.html" >&2
		rc=1
	fi
fi

exit $rc
