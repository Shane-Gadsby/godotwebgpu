#!/bin/bash
# build.sh editor|editor_dev|web : waits for the build lease, builds in the work/45 worktree, drops the lease.
WT=/mnt/109313D2109313D2/godot-editors/godotwebgpu-45
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_notes/work/lease.sh
until $L take build worker-45 2 >/dev/null 2>&1; do sleep 30; done
echo "lease taken $(date +%H:%M:%S)"
cd $WT
export PATH="$HOME/.cache/godotwebgpu-ccache-bin:$PATH" CCACHE_BASEDIR=$WT
start=$(date +%s)
case "$1" in
	editor) scons platform=linuxbsd target=editor accesskit=yes webgpu=yes gdscript_braces=yes linker=mold -j24; rc=$? ;;
	editor_dev) scons platform=linuxbsd target=editor accesskit=yes webgpu=yes gdscript_braces=yes linker=mold dev_mode=yes -j24; rc=$? ;;
	web) source ~/emsdk/emsdk_env.sh >/dev/null 2>&1; scons platform=web target=template_debug dlink_enabled=no webgpu=yes opengl3=no threads=no gdscript_braces=yes module_raycast_enabled=yes -j24; rc=$? ;;
esac
echo "build $1 rc=$rc in $(( $(date +%s) - start ))s"
$L drop build worker-45
exit $rc
