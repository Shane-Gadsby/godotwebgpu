#!/usr/bin/env bash
# Waits for the build lease, builds editor (dev_mode) + web nothreads template in the work/53 worktree.
set -uo pipefail
cd /mnt/109313D2109313D2/godot-editors/godotwebgpu-53
L=webgpu_notes/work/lease.sh
until $L take build worker-53 2 >/dev/null 2>&1; do sleep 30; done
echo "lease taken $(date +%T)"
while pgrep -ax scons | grep -q godotwebgpu-53; do sleep 10; done
export PATH="$HOME/.cache/godotwebgpu-ccache-bin:$PATH" CCACHE_MAXSIZE=50G CCACHE_BASEDIR=$PWD
rc=0
if [[ "${SKIP_EDITOR:-0}" != 1 ]]; then
scons platform=linuxbsd target=editor dev_mode=yes webgpu=yes accesskit=no -j24 > "$1/editor.log" 2>&1 || rc=1
echo "editor rc=$rc $(date +%T)"
fi
if [[ $rc == 0 ]]; then
  [[ -x bin/wgsl_convert_cli ]] || ./drivers/webgpu/wgsl_cli/build.sh > "$1/wgslcli.log" 2>&1 || rc=3
fi
if [[ $rc == 0 ]]; then
  source ~/emsdk/emsdk_env.sh >/dev/null 2>&1
  export PATH="$HOME/.cache/godotwebgpu-ccache-bin:$PATH"
  scons platform=web target=template_release dlink_enabled=no webgpu=yes opengl3=no threads=no -j24 > "$1/web.log" 2>&1 || rc=2
  echo "web rc=$rc $(date +%T)"
fi
$L drop build worker-53
echo "done rc=$rc"
