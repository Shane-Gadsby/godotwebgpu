#!/bin/bash
# render.sh <editor> <method> <WxH> <out> args...
ed=$1; m=$2; res=$3; out=$4; shift 4
xvfb-run -a -s "-screen 0 2700x1600x24" $ed --path /tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w79/proj --rendering-method $m --resolution $res -s dof_test.gd -- out=$out "$@" > $out.log 2>&1
