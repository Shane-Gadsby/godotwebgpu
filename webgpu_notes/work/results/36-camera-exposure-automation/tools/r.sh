#!/usr/bin/env bash
# r.sh <editor> <renderer> <WxH> args... : one render of meter_test.gd under xvfb (caller holds the gpu lease).
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w36
ED=$1 rm=$2 res=$3; shift 3
timeout 180 xvfb-run -a -s "-screen 0 4000x2300x24" "$ED" --path "$W/proj" --rendering-method "$rm" --resolution "$res" -s meter_test.gd -- res=$res "$@" 2>&1 | grep -vE "^(Godot Engine|Vulkan|WebGPU|OpenGL|$)" | head -20
