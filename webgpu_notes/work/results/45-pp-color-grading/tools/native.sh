#!/bin/bash
# native.sh <editor> <method> <outdir> case:chart ... : renders the grading chart per case.
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w45
ed=$1; m=$2; od=$3; shift 3; mkdir -p $od
for cc in "$@"; do c=${cc%%:*}; ch=${cc##*:}
	timeout 120 xvfb-run -a -s "-screen 0 1400x1200x24" $ed --path $W/proj --rendering-method $m --resolution 1080x1080 -s grade_test.gd -- out=$od/${c}_$ch.png case=$c chart=$ch > $od/${c}_$ch.log 2>&1 || echo "FAILED $m $c $ch"
	[ -f $od/${c}_$ch.png ] || echo "MISSING $m $c $ch"
done
