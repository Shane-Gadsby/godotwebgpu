#!/usr/bin/env bash
# One NaN pixel through every bokeh DOF mode, natively, on Forward+ (compute DOF) and Forward Mobile
# (raster DOF): see dof_nan.gd. Needs a display and a real renderer; --headless has none.
#
#   ./run.sh [editor binary] [out dir]
#
# Exit 0 when every mode keeps the NaN to the quad's own pixels.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
EDITOR="${1:-${GODOT_EDITOR_BIN:-$REPO/bin/godot.linuxbsd.editor.x86_64}}"
OUT="${2:-$HERE/out}"
[[ -x "$EDITOR" ]] || { echo "no editor at $EDITOR"; exit 1; }
[[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]] || { echo "needs a display (or run under xvfb-run)"; exit 1; }
if [[ ! -d "$HERE/.godot" ]]; then
    "$EDITOR" --headless --path "$HERE" --import >/dev/null 2>&1 || true
fi
status=0
for method in forward_plus mobile; do
    echo "== $method"
    log="$(mktemp)"
    "$EDITOR" --path "$HERE" --rendering-driver vulkan --rendering-method "$method" --resolution 640x360 \
        -- --out="$OUT/$method" >"$log" 2>&1 || true
    grep -E "^DOFNAN|Forward|Mobile" "$log" || true
    grep -q "^DOFNAN DONE failures=0" "$log" || status=1
    rm -f "$log"
done
exit $status
