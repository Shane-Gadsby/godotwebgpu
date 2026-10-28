#!/usr/bin/env bash
# Task 39 verification: run.sh <godot editor> [scratch dir]. BASE=<pre-change editor> adds the pixel-identity checks.
# Needs xvfb-run and a GPU (Vulkan for Forward+/Mobile, GL for Compatibility). Exit 1 on any FAIL.
set -u
G=$(realpath "$1"); W=${2:-$(mktemp -d)}; here=$(cd "$(dirname "$0")" && pwd); fail=0
mkdir -p "$W/out"; rm -rf "$W/proj" "$W/edproj"; cp -r "$here/proj" "$W/proj"; cp -r "$here/edproj" "$W/edproj"
o=$W/out
X() { timeout 180 xvfb-run -a -s "-screen 0 1600x1000x24" "$@"; }
# render <bin> <method> <out> [--editor] -- key=value...: task 37's verify.gd scenes, fixed 60 fps so the zebras' crawl
# is at the same phase in every run.
render() {
	local bin=$1 m=$2 out=$3; shift 3
	local pre=(); while [[ $# -gt 0 && "$1" != "--" ]]; do pre+=("$1"); shift; done; shift
	rm -f "$out"
	X "$bin" "${pre[@]}" --path "$W/proj" --rendering-method $m --resolution 1280x720 --fixed-fps 60 -s verify.gd -- out="$out" "$@" > "$out.log" 2>&1
	[ -f "$out" ] || { echo "FAIL render $out"; tail -5 "$out.log"; fail=1; }
}
motion() {
	local m=$1 out=$2; shift 2
	local pre=(); while [[ $# -gt 0 && "$1" != "--" ]]; do pre+=("$1"); shift; done; shift
	rm -f "$out"
	X "$G" "${pre[@]}" --path "$W/proj" --rendering-method $m --resolution 640x360 --fixed-fps 60 -s motion.gd -- out="$out" "$@" > "$out.log" 2>&1
	[ -f "$out" ] || { echo "FAIL render $out"; tail -5 "$out.log"; fail=1; }
	grep -q "EDITOR_HINT=$( [[ " ${pre[*]} " == *" --editor "* ]] && echo true || echo false )" "$out.log" || { echo "FAIL $out: wrong editor hint"; fail=1; }
}
P() { python3 "$here/$1" "${@:2}" || fail=1; }

echo "== presets (headless)"
timeout 600 "$G" --headless --path "$W/proj" --fixed-fps 60 -s test_presets.gd -- "$o/presets.json" 2>&1 | grep -E "PASS|FAIL|DONE" | cut -c1-200 | tee "$o/presets.txt"
grep -q "failures: 0" "$o/presets.txt" || fail=1
P presets_analyze.py "$o/presets.json"

for m in forward_plus mobile gl_compatibility; do
	echo "== zebras, $m"
	render "$G" $m "$o/${m}_base.png" -- scene=ramp
	render "$G" $m "$o/${m}_z1.png" -- scene=ramp zebra=95 vio=1
	render "$G" $m "$o/${m}_z1c.png" -- scene=ramp zebra=95 vio=1 zcol=1,0,0
	render "$G" $m "$o/${m}_z2.png" -- scene=ramp zebra2=70 vio=1
	render "$G" $m "$o/${m}_z2c.png" -- scene=ramp zebra2=70 vio=1 zcol2=0,0.5,1
	P zebra_check.py color "$o/${m}_base.png" "$o/${m}_z1.png" "$o/${m}_z1c.png" 0,0,0 1,0,0
	P zebra_check.py color "$o/${m}_base.png" "$o/${m}_z2.png" "$o/${m}_z2c.png" 1,1,1 0,0.5,1
	if [ -n "${BASE:-}" ]; then
		render "$BASE" $m "$o/${m}_base_old.png" -- scene=ramp
		render "$BASE" $m "$o/${m}_both.png" -- scene=ramp zebra=95 zebra2=70 vio=1
		render "$G" $m "$o/${m}_both_new.png" -- scene=ramp zebra=95 zebra2=70 vio=1
		render "$BASE" $m "$o/${m}_step_old.png" -- scene=step
		render "$G" $m "$o/${m}_step_new.png" -- scene=step
		echo -n "default (ramp) vs pre-change editor: "; P zebra_check.py diff "$o/${m}_base.png" "$o/${m}_base_old.png"
		echo -n "default (step) vs pre-change editor: "; P zebra_check.py diff "$o/${m}_step_new.png" "$o/${m}_step_old.png"
		echo -n "both zebras, default colors, vs pre-change editor: "; P zebra_check.py diff "$o/${m}_both_new.png" "$o/${m}_both.png"
	fi
done

echo "== tick boxes (Forward+: motion blur and rolling shutter are Forward+ only)"
for t in blur rolling; do
	for mode in game editor; do
		ed=(); [ $mode = editor ] && ed=(--editor)
		motion forward_plus "$o/${t}_${mode}_off.png" "${ed[@]}" -- test=$t fx=0
		motion forward_plus "$o/${t}_${mode}_on.png" "${ed[@]}" -- test=$t fx=1 vis=0
		motion forward_plus "$o/${t}_${mode}_onvis.png" "${ed[@]}" -- test=$t fx=1 vis=1
		exp=present; [ $mode = editor ] && exp=absent
		P effect_check.py "$t $mode, unticked" "$o/${t}_${mode}_off.png" "$o/${t}_${mode}_on.png" $exp
		P effect_check.py "$t $mode, ticked" "$o/${t}_${mode}_off.png" "$o/${t}_${mode}_onvis.png" present
	done
done
for mode in game editor; do
	ed=(); [ $mode = editor ] && ed=(--editor)
	for vis in 0 1; do
		motion forward_plus "$o/shake_${mode}_$vis.png" "${ed[@]}" -- test=shake fx=1 vis=$vis
		line=$(grep SHAKE "$o/shake_${mode}_$vis.png.log")
		n=$(sed -E 's/.*distinct_offsets=([0-9]+).*/\1/' <<<"$line"); c=$(sed -E 's/.*distinct_camera_transforms=([0-9]+).*/\1/' <<<"$line")
		if [ $mode = editor ] && [ $vis = 0 ]; then ok=$([ "$n" = 1 ] && [ "$c" = 1 ] && echo 1 || echo 0); want="holds still"; else ok=$([ "$n" -gt 20 ] && [ "$c" -gt 20 ] && echo 1 || echo 0); want="shakes"; fi
		echo "$([ $ok = 1 ] && echo PASS || echo FAIL) shake $mode, visible_in_editor=$vis: $want ($line)"; [ $ok = 1 ] || fail=1
	done
done
echo "== windowed editor (Xvfb) and the same scene as a game"
(cd "$W/edproj" && EDCHECK_OUT="$W/edcheck_editor.txt" X "$G" --path . -e main.tscn >/dev/null 2>&1)
(cd "$W/edproj" && EDCHECK_OUT="$W/edcheck_game.txt" X "$G" --path . >/dev/null 2>&1)
cat "$W/edcheck_editor.txt" "$W/edcheck_game.txt" 2>/dev/null || { echo "FAIL editor check wrote nothing"; fail=1; }
grep -q PASS "$W/edcheck_editor.txt" && grep -q PASS "$W/edcheck_game.txt" || fail=1
grep -q FAIL "$W/edcheck_editor.txt" "$W/edcheck_game.txt" && fail=1
echo "TASK 39 VERIFY: $([ $fail = 0 ] && echo OK || echo FAILED)"; exit $fail
