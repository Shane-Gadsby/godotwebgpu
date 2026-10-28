#!/bin/bash
# ident.sh <labelA> <labelB>: compares every EXR of two runs byte for byte, per renderer.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w64
for m in forward_plus mobile gl_compatibility; do
	n=0; same=0; diff=""
	for f in $S/out/$1/$m/*.exr; do
		b=$S/out/$2/$m/$(basename $f); n=$((n+1))
		if [ -f "$b" ] && cmp -s $f $b; then same=$((same+1)); else diff="$diff $(basename $f)"; fi
	done
	echo "$1 vs $2 $m: $same/$n identical${diff:+, differ:$diff}"
done
