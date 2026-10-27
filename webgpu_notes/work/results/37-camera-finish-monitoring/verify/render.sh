#!/usr/bin/env bash
# render.sh <editor-binary> <renderer> <WxH> <out.png> [extra godot flags --] key=value...
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w37
bin=$1 rm=$2 res=$3 out=$4; shift 4
pre=()
while [[ $# -gt 0 && "$1" != "--" && "$1" == --* ]]; do pre+=("$1"); shift; done
[[ "${1:-}" == "--" ]] && shift
rm -f "$out"
timeout 120 xvfb-run -a -s "-screen 0 2600x1500x24" "$bin" "${pre[@]}" --path $S/proj --rendering-method "$rm" --resolution "$res" -s verify.gd -- out="$out" "$@" > "$out.log" 2>&1
if [[ ! -f "$out" ]]; then echo "FAILED $out"; tail -20 "$out.log"; exit 1; fi
if grep -q "SCRIPT ERROR" "$out.log"; then echo "SCRIPT ERROR in $out"; grep -A3 "SCRIPT ERROR" "$out.log"; exit 1; fi
grep -h "EDITOR_HINT" "$out.log" | head -1
