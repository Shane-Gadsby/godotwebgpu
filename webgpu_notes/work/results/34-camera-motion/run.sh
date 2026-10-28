#!/usr/bin/env bash
# run.sh <method> <WxH> <out> key=val... : one native render of mbproj.
ED=${ED:-/mnt/109313D2109313D2/godot-editors/godotwebgpu-34/bin/godot.linuxbsd.editor.x86_64}
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w34
m=$1; r=$2; out=$3; shift 3
timeout 120 xvfb-run -a -s "-screen 0 2600x1500x24" $ED --path $S/mbproj --rendering-method $m --resolution $r --fixed-fps 60 --disable-vsync -- out=$out "$@" 2>&1 | grep -E "SAVED|ERROR|error|Mismatch|WARN" | head -8
