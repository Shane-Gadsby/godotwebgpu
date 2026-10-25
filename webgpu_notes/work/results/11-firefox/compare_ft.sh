#!/usr/bin/env bash
# compare_ft.sh <out dir> <runs> <scene>... -- tagA=exportsA tagB=exportsB [...]
#
# Frame-time A/B for browsers without timestamp queries (task 11, Firefox): task 08's compare.sh with
# perf_guard/frame_time.mjs in place of gpu_time.mjs. BROWSER (default firefox) picks the browser; one uncapped launch
# per (run, scene, config), the order of the configs rotated every run; same prechecks (no SCons, load, VRAM, GPU
# utilization, no game or LM Studio on the GPU) and the same CONTAMINATED marking. Logs: <out>/<scene>.<tag>.<run>.log;
# summarize_ft.mjs tabulates. Resumable.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FT="$HERE/../../../../webgpu_tests/perf_guard/frame_time.mjs"
out=$1 runs=$2; shift 2
scenes=(); while [[ $1 != -- ]]; do scenes+=("$1"); shift; done; shift
configs=("$@")
mkdir -p "$out"
BROWSER=${BROWSER:-firefox}
LOAD_MAX=${LOAD_MAX:-4} VRAM_MAX_MIB=${VRAM_MAX_MIB:-4000} WAIT_MAX=${WAIT_MAX:-600} UTIL_MAX=${UTIL_MAX:-25}
BUSY_APPS=${BUSY_APPS:-Oblivion|LM Studio|lm-studio|llama}
SETTLE=${SETTLE:-12000} WINDOW=${WINDOW:-10000}
precheck() {
	local waited=0
	while :; do
		local load vram scons util busy
		load=$(cut -d' ' -f1 /proc/loadavg)
		vram=$(nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | head -1)
		scons=$(pgrep -cx scons || true)
		util=$(for _ in 1 2 3; do nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits | head -1; sleep 1; done | sort -n | tail -1)
		busy=$(nvidia-smi --query-compute-apps=name --format=csv,noheader | grep -cE "$BUSY_APPS" || true)
		if [[ $scons == 0 ]] && awk "BEGIN{exit !($load < $LOAD_MAX)}" && ((vram < VRAM_MAX_MIB)) && ((util < UTIL_MAX)) && ((busy == 0)); then return 0; fi
		((waited >= WAIT_MAX)) && { echo "PRECHECK FAILED: load $load, VRAM $vram MiB, util $util%, busy apps $busy, scons $scons" >&2; exit 3; }
		echo "waiting: load $load, VRAM $vram MiB, util $util%, busy apps $busy, scons $scons" >&2
		sleep 20; waited=$((waited + 20))
	done
}
postcheck() { if nvidia-smi --query-compute-apps=name --format=csv,noheader | grep -qE "$BUSY_APPS"; then echo "CONTAMINATED: $BUSY_APPS on the GPU at the end" >>"$1"; echo "  contaminated: $1"; fi; }
n=${#configs[@]}
for ((run = 1; run <= runs; run++)); do
	for scene in "${scenes[@]}"; do
		for ((i = 0; i < n; i++)); do
			c=${configs[$(((i + run - 1) % n))]}
			tag=${c%%=*} dir=${c#*=}
			log="$out/$scene.$tag.$run.log"
			if grep -q "ms/frame" "$log" 2>/dev/null && ! grep -q CONTAMINATED "$log"; then continue; fi
			precheck
			echo "$(date +%T) run $run $scene $tag (load $(cut -d' ' -f1 /proc/loadavg))"
			node "$FT" --dir "$dir/$scene" --browser "$BROWSER" --uncapped --settle "$SETTLE" --window "$WINDOW" >"$log" 2>&1 || echo "  FAILED: $log"
			postcheck "$log"
		done
	done
done
