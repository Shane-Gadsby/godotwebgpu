#!/usr/bin/env bash
# render_all.sh <editor> <outdir> <WxH> <layout> cases...: native renders in all three renderers under the gpu lease.
set -uo pipefail
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w47
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu-47/webgpu_notes/work/lease.sh
ED=$1; OUT=$2; RES=$3; LAYOUT=$4; shift 4
until $L take gpu worker-47 1 >/dev/null 2>&1; do echo "waiting for gpu lease $(date +%T)"; sleep 30; done
for rm in forward_plus mobile gl_compatibility; do
	mkdir -p "$OUT/$rm"
	for c in "$@"; do
		timeout 120 xvfb-run -a -s "-screen 0 2600x1500x24" "$ED" --path "$W/proj" --rendering-method $rm --resolution $RES -s panini_test.gd -- "$c" "$OUT/$rm" "$LAYOUT" "$RES" 2>&1 | grep -E "captured|ERROR|SCRIPT ERROR" | head -3 | sed "s/^/$rm $c: /"
	done
done
$L drop gpu worker-47 >/dev/null
