#!/usr/bin/env bash
# run_native.sh <base|new> <outfile> <renderer> key=val ... : one native capture run under Xvfb (Vulkan), fixed 60 fps.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w74
build=$1; out=$2; method=$3; shift 3
timeout 300 xvfb-run -a -s "-screen 0 1920x1080x24" "$S/$build/godot.linuxbsd.editor.x86_64" --path "$S/bench" \
	--rendering-driver vulkan --rendering-method "$method" --fixed-fps 60 -- "$@" 2>&1 | grep -E '^\[cap\]|ERROR|SCRIPT' >"$out"
grep -q '"kind":"done"' "$out" || { echo "FAILED: $build $method $*"; tail -3 "$out"; exit 1; }
