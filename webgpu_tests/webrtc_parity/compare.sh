#!/usr/bin/env bash
# Runs one fixture on native and in a browser, normalizes the volatile parts of
# the output, and diffs them. Any surviving line is a parity divergence.
#
#   ./compare.sh mesh
#   ./compare.sh loopback
#
# Needs: a built native editor, an export already in exports/loopback/, and a
# server on $PORT serving it. See webgpu_notes/webrtc_web.md §4.
set -uo pipefail

TEST="${1:-loopback}"
PORT="${PORT:-8099}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EDITOR="${EDITOR_BIN:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"
PROJ="$ROOT/webgpu_tests/webrtc_parity/fixtures/loopback_project"
OUT="$(mktemp -d)"

# Object ids, impl class names, tick counts and SDP lengths all legitimately
# differ run to run or platform to platform; nothing else may.
norm() {
	sed -E -e 's/#-?[0-9]+//g' \
	       -e 's/<([A-Za-z]+)>/\1/g' \
	       -e 's/WebRTC(Lib|)(PeerConnection|DataChannel)(JS|)/IMPL/g' \
	       -e 's/after [0-9]+ ticks/after N ticks/' \
	       -e 's/len=[0-9]+/len=N/' "$1"
}

"$EDITOR" --headless --path "$PROJ" -- "$TEST" 2>&1 | grep -oE "RTC\|.*" > "$OUT/native.txt"
RTC_URL="http://localhost:$PORT/index.html?test=$TEST" node "$(dirname "${BASH_SOURCE[0]}")/run_loopback.mjs" 2>&1 \
	| grep -oE "RTC\|.*" > "$OUT/web.txt"

norm "$OUT/native.txt" > "$OUT/n.txt"
norm "$OUT/web.txt"    > "$OUT/w.txt"

echo "native: $(wc -l < "$OUT/n.txt") lines, web: $(wc -l < "$OUT/w.txt") lines"
if diff -u "$OUT/n.txt" "$OUT/w.txt" > "$OUT/diff.txt"; then
	echo "PARITY: identical"
	rc=0
else
	echo "PARITY: divergences below (native = -, web = +)"
	cat "$OUT/diff.txt"
	rc=1
fi
echo "raw output kept in $OUT"
exit $rc
