#!/usr/bin/env bash
# Default folders test: drives the Create Script/Shader/Scene dialogs and the editor's Save Scene/Resource As dialog
# in a windowed editor under Xvfb, and checks the folders they propose and the `editor/default_folders/*` settings
# their "Make this the default folder" check boxes write. Runs on a scratch copy: the check boxes save project.godot.
# EDITOR_BIN overrides the editor.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EDITOR="${EDITOR_BIN:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"
command -v xvfb-run >/dev/null || { echo "SKIP -- xvfb-run not installed"; exit 2; }
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp -r "$ROOT/tests/default_folders/project" "$WORK/project"
"$EDITOR" --headless --path "$WORK/project" --import >/dev/null 2>&1 || true
timeout 300 xvfb-run -a -s "-screen 0 1920x1080x24" "$EDITOR" --editor --path "$WORK/project" --rendering-driver opengl3 2>&1 | grep -E "\[DF-TEST\]|SCRIPT ERROR|ERROR:"
exit "${PIPESTATUS[0]}"
