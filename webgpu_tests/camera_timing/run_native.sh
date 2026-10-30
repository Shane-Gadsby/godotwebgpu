#!/usr/bin/env bash
# Camera frame rate, frame hold, shutter angle and light flicker (work tasks 85-87), natively (Vulkan, Forward+) under
# xvfb-run. EDITOR_BIN overrides the editor; extra arguments go to it (e.g. --rendering-method mobile). Exit 0 = pass.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PROJ="$ROOT/webgpu_tests/camera_timing/project"
EDITOR="${EDITOR_BIN:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"
[[ -x "$EDITOR" ]] || { echo "no editor at $EDITOR" >&2; exit 2; }
out=$(timeout 180 xvfb-run -a -s "-screen 0 1280x720x24" "$EDITOR" --path "$PROJ" --rendering-driver vulkan --fixed-fps 60 --script main.gd "$@" 2>&1)
echo "$out" | grep -E "ERROR|SCRIPT ERROR" | head -10
echo "$out" | grep "^CT RESULT " | sed 's/^CT RESULT //' | python3 -c '
import json, sys
r = json.load(sys.stdin)
for k, v in sorted(r["results"].items()):
    print("%s %s %s" % ("PASS" if v["ok"] else "FAIL", k, {a: b for a, b in v.items() if a != "ok"}))
print("%d/%d passed" % (r["passed"], r["total"]))
sys.exit(0 if r["passed"] == r["total"] else 1)'
rc=$?
echo "$out" | grep -q "^CT DONE" || { echo "no CT DONE" >&2; exit 1; }
exit $rc
