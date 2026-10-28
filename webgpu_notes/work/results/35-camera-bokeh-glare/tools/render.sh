#!/usr/bin/env bash
# render.sh <editor> <forward_plus|mobile|gl_compatibility> <WxH> <out.png> [script args...]
ed=$1; rm=$2; res=$3; out=$4; shift 4
timeout 120 xvfb-run -a -s "-screen 0 2600x1500x24" "$ed" --path <scratch>/proj --rendering-method "$rm" --resolution "$res" -s glare_test.gd -- out="$out" "$@" > "${out%.png}.log" 2>&1
rc=$?
[[ -f "$out" ]] || { echo "FAILED $out rc=$rc"; tail -20 "${out%.png}.log"; exit 1; }
grep -E "ERROR|SCRIPT ERROR|Shader|error" "${out%.png}.log" | grep -v "^$" | head -5
