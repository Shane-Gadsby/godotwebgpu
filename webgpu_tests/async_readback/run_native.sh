#!/usr/bin/env bash
# Runs the async readback project natively (Vulkan, Forward+) under xvfb-run and prints its W66RESULT line.
# EDITOR_BIN overrides the editor. Exit status: the project's (0 = every check passed).
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PROJ="$ROOT/webgpu_tests/async_readback/project"
EDITOR="${EDITOR_BIN:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"
[[ -x "$EDITOR" ]] || { echo "no editor at $EDITOR" >&2; exit 2; }
"$EDITOR" --headless --path "$PROJ" --import >/dev/null 2>&1 || true
out=$(timeout 120 xvfb-run -a "$EDITOR" --path "$PROJ" --rendering-driver vulkan "$@" 2>&1)
rc=$?
echo "$out" | grep -E "^W66|ERROR|SCRIPT ERROR" | head -40
echo "$out" | grep "^W66RESULT " | sed 's/^W66RESULT //' | python3 -c '
import json, sys
r = json.load(sys.stdin)
for k, v in sorted(r["results"].items()):
    if not v["ok"]:
        print("FAIL %s: %s" % (k, v["error"]))
print("%d/%d passed (native)" % (r["passed"], r["total"]))'
echo "$out" | grep -q "^W66DONE" || { echo "no W66DONE (rc=$rc)" >&2; exit 1; }
# The web's "not back yet" warning (task 71) is web-only: a synchronous read natively returns the pixels.
warns=$(echo "$out" | grep -c "returned null for")
echo "sync readback warnings: $warns (native, expected 0)"
[[ "$warns" == 0 ]] || exit 1
exit $rc
