#!/usr/bin/env bash
# build_both.sh: editor (dev_mode) + web template_debug in worktree 57, under the build lease (waits for it).
set -uo pipefail
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w57
R=/mnt/109313D2109313D2/godot-editors/godotwebgpu-57
L=$R/webgpu_notes/work/lease.sh
cd $R
until $L take build worker-57 2 >/dev/null 2>&1; do echo "waiting for build lease $(date +%T): $($L show | grep build)"; sleep 60; done
pgrep -ax scons | grep -q "$R" && { echo "scons running in this tree"; $L drop build worker-57; exit 1; }
export PATH="$HOME/.cache/godotwebgpu-ccache-bin:$PATH" CCACHE_BASEDIR=$R
scons platform=linuxbsd target=editor accesskit=yes webgpu=yes gdscript_braces=yes linker=mold dev_mode=yes -j24 > $S/build_editor2.log 2>&1
echo "editor exit=$?"
if [[ "${1:-}" != "editor" ]]; then
	(source ~/emsdk/emsdk_env.sh >/dev/null 2>&1; scons platform=web target=template_debug dlink_enabled=no webgpu=yes opengl3=no threads=no gdscript_braces=yes module_raycast_enabled=yes -j24 > $S/build_web2.log 2>&1; echo "web exit=$?")
fi
$L drop build worker-57
grep -n ": error:\|: warning:" $S/build_editor2.log $S/build_web2.log 2>/dev/null | head
grep -n "WGSL Precompile\] Results" $S/build_web2.log 2>/dev/null
