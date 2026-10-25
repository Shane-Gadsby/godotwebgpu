#!/usr/bin/env bash
# Task 18: native Vulkan capture of the scratch cameraSim copy (with the t18_capture.gd autoload), on the real
# display (DISPLAY=:0; --headless has no GPU). --fixed-fps 60 makes every frame's delta 1/60 s, the autoload caps
# rendering at 60 fps, as it does on the web. Under the gpu lease.
#   capture_native.sh <scratch_project> <out_dir>
set -euo pipefail
proj="${1:?project}"; out="$(mkdir -p "${2:?out}" && cd "$2" && pwd)"
editor=/mnt/109313D2109313D2/godot-editors/godotwebgpu/bin/godot.linuxbsd.editor.x86_64
export T18_OUT="$out" DISPLAY="${DISPLAY:-:0}"
t0=$(date +%s.%N)
timeout 300 systemd-run --user --scope -q -p MemoryMax=10G -p MemorySwapMax=0 "$editor" --path "$proj" \
	--rendering-driver vulkan --resolution 1920x1080 --windowed --fixed-fps 60 >"$out/native.log" 2>&1 || echo "exited $?"
echo "native run took $(echo "$(date +%s.%N) - $t0" | bc) s"
grep -E '^\[t18\]|Vulkan|Forward' "$out/native.log" | head -30
ls "$out"
