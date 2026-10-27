#!/usr/bin/env bash
# Task 38 verification: run.sh <godot editor binary> [scratch dir]. Headless AF tests at 30/60/144 fps, the shake
# recording + numpy spectral analysis, the windowed editor check (Xvfb) and the shake-off pixel identity per renderer
# (BASE=<pre-change editor> to compare against; skipped without it). Exit 1 on any FAIL.
set -u
G=$(realpath "$1"); W=${2:-$(mktemp -d)}; here=$(cd "$(dirname "$0")" && pwd); fail=0
mkdir -p "$W/proj" "$W/edproj" "$W/rproj"
printf 'config_version=5\n[application]\nconfig/name="t38"\n' > "$W/proj/project.godot"
cp "$here"/test_af.gd "$here"/test_shake.gd "$here"/shake_analyze.py "$W/proj/"
cp "$here"/edproj/* "$W/edproj/"; cp "$here/render.gd" "$W/rproj/"; cp "$W/proj/project.godot" "$W/rproj/"
for fps in 30 60 144; do
	echo "== AF at $fps fps"; timeout 300 "$G" --headless --path "$W/proj" --fixed-fps $fps -s test_af.gd -- "$W/af_$fps.json" 2>&1 | grep "PASS\|FAIL\|DONE" | tee "$W/af_$fps.txt"
	grep -q "failures: 0" "$W/af_$fps.txt" || fail=1
done
echo "== shake"; timeout 300 "$G" --headless --path "$W/proj" --fixed-fps 60 -s test_shake.gd -- "$W/shake.json" 2>&1 | grep "PASS\|FAIL\|DONE" | tee "$W/shake.txt"
grep -q "failures: 0" "$W/shake.txt" || fail=1
python3 "$W/proj/shake_analyze.py" "$W/shake.json" || fail=1
echo "== editor (Xvfb)"; (cd "$W/edproj" && EDCHECK_OUT="$W/edcheck.txt" timeout 180 xvfb-run -a -s "-screen 0 1920x1080x24" "$G" --path . -e main.tscn >/dev/null 2>&1)
grep "PASS\|FAIL" "$W/edcheck.txt" || { echo "FAIL editor check wrote nothing"; fail=1; }
grep -q FAIL "$W/edcheck.txt" && fail=1
if [ -n "${BASE:-}" ]; then
	for m in forward_plus mobile gl_compatibility; do
		for b in base new; do
			bin=$G; [ $b = base ] && bin=$BASE
			timeout 120 xvfb-run -a -s "-screen 0 1280x800x24" "$bin" --path "$W/rproj" --rendering-method $m --resolution 640x360 --fixed-fps 60 -s render.gd -- "$W/r_${m}_$b.png" 0 >/dev/null 2>&1
		done
		python3 -c "
from PIL import Image; import numpy as np, sys
L=lambda n: np.asarray(Image.open(n).convert('RGB')).astype(int)
d=np.abs(L('$W/r_${m}_base.png')-L('$W/r_${m}_new.png')).max(); print(('PASS' if d==0 else 'FAIL'),'$m shake-off pixel identity, max diff',d); sys.exit(int(d != 0))" || fail=1
	done
else
	echo "SKIP pixel identity (set BASE=<pre-change editor>)"
fi
echo "TASK 38 VERIFY: $([ $fail = 0 ] && echo OK || echo FAILED)"; exit $fail
