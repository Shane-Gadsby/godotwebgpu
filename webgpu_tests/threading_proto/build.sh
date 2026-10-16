#!/usr/bin/env bash
# Standalone proof of the web render-thread mechanics (TASKS.md 2.5): a pthread that owns the canvas as an
# OffscreenCanvas, creates its own WebGPU device, wakes on Atomics.waitAsync and yields after every frame.
#   ./build.sh && node run.mjs "logic=8&render=8"
set -euo pipefail
cd "$(dirname "$0")"
command -v em++ >/dev/null || source "${EMSDK_ENV:-$HOME/emsdk/emsdk_env.sh}" >/dev/null
em++ main.cpp -o index.html --shell-file shell.html -O2 -pthread --use-port=emdawnwebgpu -sOFFSCREENCANVAS_SUPPORT=1 \
	-sPTHREAD_POOL_SIZE=2 -sEXIT_RUNTIME=0 -sEXPORTED_FUNCTIONS=_main,_render_thread_device_ready
