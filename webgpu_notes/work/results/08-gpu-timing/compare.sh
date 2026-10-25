#!/usr/bin/env bash
# compare.sh <out dir> <runs> <scene>... -- tagA=exportsA tagB=exportsB [...]
#
# GPU timing A/B of the same scenes exported with different converters (task 08; tasks 11/12 reuse the method).
# Each <exports> directory holds one export per scene at <exports>/<scene>/index.html. For every run the order of the
# configurations is rotated (run 1: A B, run 2: B A, ...), so drift over the session (thermals, a background process)
# lands on both sides. Each (run, scene, config) does two gpu_time.mjs launches: per command buffer (GPU ms/frame and
# fps) and --passes (per-pass GPU ms/frame). Logs: <out>/<scene>.<tag>.<run>.{cb,passes}.log; summarize.mjs tabulates.
# Precheck before every launch: no SCons anywhere, 1-minute load under LOAD_MAX (default 4), nvidia-smi VRAM in use
# under VRAM_MAX_MIB (default 4000), GPU utilization (max of 3 samples 1 s apart) under UTIL_MAX % (default 25),
# and no game or LM Studio among nvidia-smi's processes (BUSY_APPS). A failed check waits, then refuses after WAIT_MAX seconds; it never measures anyway.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GPU_TIME="$HERE/../../../../webgpu_tests/perf_guard/gpu_time.mjs"
out=$1 runs=$2; shift 2
scenes=(); while [[ $1 != -- ]]; do scenes+=("$1"); shift; done; shift
configs=("$@")
mkdir -p "$out"
LOAD_MAX=${LOAD_MAX:-4} VRAM_MAX_MIB=${VRAM_MAX_MIB:-4000} WAIT_MAX=${WAIT_MAX:-600} UTIL_MAX=${UTIL_MAX:-25}
BUSY_APPS=${BUSY_APPS:-Oblivion|LM Studio|lm-studio|llama}
SETTLE=${SETTLE:-12000} ROUNDS=${ROUNDS:-4}
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
# A game started during a launch: mark its log so summarize.mjs drops it (the next precheck then waits).
postcheck() { if nvidia-smi --query-compute-apps=name --format=csv,noheader | grep -qE "$BUSY_APPS"; then echo "CONTAMINATED: $BUSY_APPS on the GPU at the end" >>"$1"; echo "  contaminated: $1"; fi; }
n=${#configs[@]}
for ((run = 1; run <= runs; run++)); do
	for scene in "${scenes[@]}"; do
		for ((i = 0; i < n; i++)); do
			c=${configs[$(((i + run - 1) % n))]}
			tag=${c%%=*} dir=${c#*=}
			precheck
			echo "$(date +%T) run $run $scene $tag (load $(cut -d' ' -f1 /proc/loadavg))"
			node "$GPU_TIME" --dir "$dir/$scene" --uncapped --settle "$SETTLE" >"$out/$scene.$tag.$run.cb.log" 2>&1 || echo "  cb FAILED"
			postcheck "$out/$scene.$tag.$run.cb.log"
			precheck
			node "$GPU_TIME" --dir "$dir/$scene" --uncapped --settle "$SETTLE" --passes --rounds "$ROUNDS" --top 400 >"$out/$scene.$tag.$run.passes.log" 2>&1 || echo "  passes FAILED"
			postcheck "$out/$scene.$tag.$run.passes.log"
		done
	done
done
