#!/usr/bin/env bash
#
# Applies this fork's Emscripten toolchain patches, and tells you loudly when the
# toolchain has moved out from under them.
#
#   ./apply.sh --status   what is applied, against which toolchain  (default)
#   ./apply.sh --check    exit 0 if ALL patches are applied, 1 if not. For CI and scons.
#   ./apply.sh --apply    apply (idempotent -- re-running is a no-op)
#   ./apply.sh --revert   restore the upstream text
#
# This is a convenience wrapper. Every patch, all the logic, and the
# toolchain-location logic live in emsdk_patch.py next to it, which can be run
# directly and is what callers without bash should use:
#
#   python3 misc/emsdk_patches/emsdk_patch.py apply      # same thing, no shell
#
# (build-windows.ps1 does exactly that.) Adding a patch means adding a table
# entry in emsdk_patch.py plus a .patch file, not editing shell. Read the .patch
# files in this directory for what each change is and why.
#
# Needed for `platform=web dlink_enabled=yes` builds. `threads=no` is where it
# matters least (0002's symptom is pthread-worker-only and 0001 cannot fire
# without threads at all), but both patches are correct for every dlink build and
# this is all-or-nothing, so just apply it.
#
# `emsdk install` overwrites the toolchain tree, so this has to be re-applied
# after EVERY toolchain change, not just after an emsdk version bump. Every build
# helper in this repo (build.sh, build-linux.sh, build-macos.sh,
# build-windows.ps1) and both web CI workflows do that themselves.
#
# scons DOES now relink when the toolchain moves -- platform/web/detect.py hashes
# the toolchain's JS glue into the link's dependencies -- so there is nothing to
# delete by hand after running this. Before that existed, a stale bin/godot*.js
# silently survived an --apply and reproduced an already-fixed crash for hours.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

case "${1:---status}" in
	--check)  ACTION=check ;;
	--apply)  ACTION=apply ;;
	--revert) ACTION=revert ;;
	--status) ACTION=status ;;
	*) echo "usage: $0 [--status|--check|--apply|--revert]" >&2; exit 2 ;;
esac

# -I: ignore PYTHON* env vars and keep the script's directory off sys.path for
# anything but the module itself -- this runs inside other people's build
# scripts and should not pick things up from the environment.
exec python3 -I "$HERE/emsdk_patch.py" "$ACTION"
