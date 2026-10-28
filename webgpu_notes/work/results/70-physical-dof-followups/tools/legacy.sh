#!/bin/bash
# legacy.sh <outdir>: dof_legacy_gather (new editor, physical + opt-in + legacy) against the pre-change editor drawing
# the same thin-lens size through upstream's physical-mode path (CameraAttributesPractical, transitions -1, amount and
# FOV from matched.py: camera-sim's own pre-63 setup).
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w70
NEW=/mnt/109313D2109313D2/godot-editors/godotwebgpu-70/bin/godot.linuxbsd.editor.x86_64
OLD=$S/godot_pre70
out=$1; mkdir -p $out
run() { # name res method "lens args" f N focus shape quality
	local name=$1 res=$2 m=$3 lens=$4 f=$5 n=$6 u=$7 rest=$8
	local H=${res#*x}
	local matched=$(python3 $S/tools/matched.py $f $n $u 24 $H)
	$S/tools/render.sh $NEW $m $res $out/${m}_${name}_legacy.png phys=1 legacy=1 f=$f n=$n focus=$u $lens $rest
	$S/tools/render.sh $OLD $m $res $out/${m}_${name}_pre.png kind=practical $matched $lens $rest
	$S/tools/render.sh $NEW $m $res $out/${m}_${name}_exact.png phys=1 f=$f n=$n focus=$u $lens $rest
}
for m in forward_plus mobile; do
	for q in 0 1 2 3; do run hex$q 1280x720 $m "dist=5" 50 1.4 2.5 "shape=hexagon quality=$q"; done
	run box1 1280x720 $m "dist=5" 50 1.4 2.5 "shape=box quality=1"
	run box3 1280x720 $m "dist=5" 50 1.4 2.5 "shape=box quality=3"
	run circ2 1280x720 $m "dist=5" 50 1.4 2.5 "shape=circle quality=2"
	run hex2_near 1280x720 $m "dist=3" 50 1.4 8 "shape=hexagon quality=2"
	run hex2_1440 2560x1440 $m "dist=5" 50 1.4 2.5 "shape=hexagon quality=2"
	run circ2_long_1440 2560x1440 $m "dist=50" 200 2.8 5 "shape=circle quality=2"
done
