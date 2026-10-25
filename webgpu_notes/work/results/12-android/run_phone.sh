#!/usr/bin/env bash
# run_phone.sh [out dir]: the whole task 12 phone session, resumable (phone_bench.mjs skips launches already on disk).
# Needs: the Razr 60 on USB with debugging allowed (`adb devices` says `device`), the `phone` lease, and the two export
# sets below (built by export_all.sh at e99b18555e; see SUMMARY.md). No desktop GPU is used.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
A="$HERE/../../../../webgpu_tests/perf_guard/android"
W=${W:-/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/06ad18c7-2f4b-4e67-91c1-7151308515f2/scratchpad/w12}
OUT=${1:-$W/phone}
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_notes/work/lease.sh
OWNER=${OWNER:-worker-12}
adb devices | grep -qw device$ || { echo "no phone: adb devices lists none (plug the Razr 60 in, allow USB debugging)"; exit 1; }
$L take phone "$OWNER" 3
# Keep the screen on while plugged in for the session (a 30 s screen timeout ends a launch), and put it back.
STAY=$(adb shell settings get global stay_on_while_plugged_in | tr -d '\r')
adb shell settings put global stay_on_while_plugged_in 7
trap 'adb shell settings put global stay_on_while_plugged_in "$STAY"; $L drop phone "$OWNER"' EXIT
CONFIGS=${CONFIGS:-"tint=$W/exp/tint,sc19=$W/exp/sc19"}
# Firefox for Android is out of scope (user, 2026-10-25: "I only care about Firefox for desktop"). BROWSERS=chrome,firefox to try it.
BROWSERS=${BROWSERS:-chrome}
echo "browsers: $BROWSERS"
# 1. Does each browser work at all, and which renderer does each scene get: one launch per scene and browser, with shots.
node "$A/phone_bench.mjs" --out "$OUT/smoke" --configs "$CONFIGS" --browsers "$BROWSERS" --runs 1 --settle 15000 --measure 5000 --shots
# 2. Forward+ frames at 640x360 against the desktop Vulkan reference (fp_check's Look set, 17 configs).
node "$A/phone_bench.mjs" --out "$OUT/sweep" --configs "$CONFIGS" --browsers "$BROWSERS" --sweep look
# 3. Timing: 3 runs, config order rotated per run, cool phone before every launch.
node "$A/phone_bench.mjs" --out "$OUT/timing" --configs "$CONFIGS" --browsers "$BROWSERS" --runs 3 --settle 20000 --measure 10000
node "$A/phone_summary.mjs" "$OUT/smoke"
node "$A/phone_summary.mjs" "$OUT/sweep" --ref "$W/native_ref"
node "$A/phone_summary.mjs" "$OUT/timing"
