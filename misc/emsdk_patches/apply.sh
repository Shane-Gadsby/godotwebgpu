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
# This script only locates the toolchain; every patch and all the logic lives in
# emsdk_patch.py next to it, so adding a patch means adding a table entry there,
# not editing shell. Read the .patch files in this directory for what each change
# is and why.
#
# Needed for `platform=web dlink_enabled=yes` builds. `threads=no` is where it
# matters least (0002's symptom is pthread-worker-only and 0001 cannot fire
# without threads at all), but both patches are correct for every dlink build and
# apply.sh is all-or-nothing, so just apply it.
#
# `emsdk install` overwrites the toolchain tree, so this has to be re-applied
# after EVERY toolchain change, not just after an emsdk version bump.
# build-linux.sh does that itself, right after its own `emsdk install`.
#
# scons DOES now relink when the toolchain moves -- platform/web/detect.py hashes
# the toolchain's JS glue into the link's dependencies -- so there is nothing to
# delete by hand after running this. Before that existed, a stale bin/godot*.js
# silently survived an --apply and reproduced an already-fixed crash for hours.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- locate the toolchain -----------------------------------------------------
# Resolved from the environment rather than hard-coded, so this works on a
# machine whose emsdk is not at ~/emsdk.
if [[ -n "${EMSCRIPTEN_ROOT:-}" ]]; then
	EM_ROOT="$EMSCRIPTEN_ROOT"
elif command -v emcc >/dev/null 2>&1; then
	EM_ROOT="$(dirname "$(readlink -f "$(command -v emcc)")")"
elif [[ -n "${EMSDK:-}" && -d "$EMSDK/upstream/emscripten" ]]; then
	EM_ROOT="$EMSDK/upstream/emscripten"
else
	echo "error: cannot find Emscripten. Source emsdk_env.sh, or set EMSCRIPTEN_ROOT." >&2
	exit 2
fi

VERSION_FILE="$EM_ROOT/emscripten-version.txt"
EM_VERSION="$(tr -d '"' < "$VERSION_FILE" 2>/dev/null || echo unknown)"

case "${1:---status}" in
	--check)  ACTION=check ;;
	--apply)  ACTION=apply ;;
	--revert) ACTION=revert ;;
	--status) ACTION=status ;;
	*) echo "usage: $0 [--status|--check|--apply|--revert]" >&2; exit 2 ;;
esac

# -I: don't put the script's directory on sys.path for imports beyond the module
# itself, and ignore PYTHON* env vars -- this runs inside other people's build
# scripts and should not pick anything up from the environment.
exec python3 -I "$HERE/emsdk_patch.py" --root "$EM_ROOT" --version "$EM_VERSION" "$ACTION"
