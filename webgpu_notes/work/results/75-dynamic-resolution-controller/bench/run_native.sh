#!/usr/bin/env bash
# run_native.sh <pre|new> <outfile> <renderer> [xvfb|display] key=val ... : one native run (Vulkan).
# xvfb: under Xvfb with --fixed-fps 60 (deterministic captures); display: windowed on the real display (timing).
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w75
build=$1; out=$2; method=$3; where=$4; shift 4
if [[ $where == xvfb ]]; then
	timeout 300 xvfb-run -a -s "-screen 0 1920x1080x24" "$S/$build/godot.linuxbsd.editor.x86_64" --path "$S/bench" \
		--rendering-driver vulkan --rendering-method "$method" --fixed-fps 60 --verbose -- "$@" >"$out.raw" 2>&1
else
	DISPLAY=${DISPLAY:-:0} timeout 400 "$S/$build/godot.linuxbsd.editor.x86_64" --path "$S/bench" \
		--rendering-driver vulkan --rendering-method "$method" --verbose -- "$@" >"$out.raw" 2>&1
fi
grep -E '^\[dr\]|Dynamic 3D|ERROR|SCRIPT|Vulkan .* - ' "$out.raw" >"$out"
grep -q '"kind":"done"' "$out" || { echo "FAILED: $build $method $*"; tail -5 "$out.raw"; exit 1; }
