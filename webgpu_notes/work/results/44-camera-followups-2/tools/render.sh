#!/bin/bash
# render.sh <editor> <method> <out> args...
ed=$1; m=$2; out=$3; shift 3
xvfb-run -a -s "-screen 0 1400x900x24" $ed --path /tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w44/proj --rendering-method $m --resolution 1280x720 -s lca_test.gd -- out=$out "$@" > $out.log 2>&1
