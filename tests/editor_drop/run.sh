#!/usr/bin/env bash
# Editor drag-and-drop test: drives real drops onto the inspector's collection editors (Array, Dictionary, property
# arrays) and the Project Settings lists, in a windowed editor under Xvfb (headless gives the editor a 2x2 viewport,
# so no drop could land). Runs on a scratch copy: the Project Settings panels save project.godot.
# EDITOR_BIN overrides the editor.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EDITOR="${EDITOR_BIN:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"
command -v xvfb-run >/dev/null || { echo "SKIP -- xvfb-run not installed"; exit 2; }
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp -r "$ROOT/tests/editor_drop/project" "$WORK/project"
"$EDITOR" --headless --path "$WORK/project" --import >/dev/null 2>&1 || true
DROP_TEST_DEBUG="${DROP_TEST_DEBUG:-}" timeout 300 xvfb-run -a -s "-screen 0 1920x1080x24" "$EDITOR" --editor --path "$WORK/project" --rendering-driver opengl3 2>&1 | grep -E "\[DROP-(TEST|DEBUG)\]"
exit "${PIPESTATUS[0]}"
