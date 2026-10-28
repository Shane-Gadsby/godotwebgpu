#!/bin/bash
# render.sh <editor> <method> <outdir> <cases file>: renders each case of the file (name + query-style args) natively.
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w56
ed=$1; m=$2; od=$3; cf=$4; mkdir -p $od
while read -r name query; do
	[ -z "$name" ] && continue
	args=$(echo "$query" | tr '&' ' ')
	timeout 120 xvfb-run -a -s "-screen 0 1300x1300x24" $ed --path $W/proj --rendering-method $m --resolution 1024x1024 -s tm_test.gd -- out=$od/$name.png $args > $od/$name.log 2>&1 < /dev/null || echo "FAILED $m $name"
	[ -f $od/$name.png ] || echo "MISSING $m $name"
done < $cf
