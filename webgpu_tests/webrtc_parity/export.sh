#!/usr/bin/env bash
# Exports the WebRTC fixture project to exports/loopback (nothreads),
# exports/loopback_threads (threads), exports/loopback_dlink (GDExtension
# support) and exports/loopback_threads_dlink (both), from the editor and web templates in bin/. Mirrors
# forward_plus/export.sh so local_ci.sh can call it the same way.
#
# EDITOR_BIN overrides the editor; RTC_THREADS=no and RTC_DLINK=no skip those
# exports, RTC_DLINK=no the threads + dlink one too (the templates for them may
# not be built). Each is skipped
# automatically when its template is absent.
#
# RTC_TEMPLATE_ZIP overrides which web template the nothreads export uses, the
# way scene_smoketest takes GODOT_TEMPLATE_ZIP. CI builds only the *dlink*
# template, so it passes that one; `variant/extensions_support` is derived from
# the filename, as run_scenes.mjs does, because a dlink template without it
# exports a page whose loader expects a side module that was never copied.
# Setting it implies RTC_THREADS=no and RTC_DLINK=no, since the override is
# applied to every preset in the file.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TIER="$ROOT/webgpu_tests/webrtc_parity"
PROJ="$TIER/fixtures/loopback_project"
EDITOR="${EDITOR_BIN:-$ROOT/bin/godot.linuxbsd.editor.x86_64}"

if [[ ! -x "$EDITOR" ]]; then
	echo "no editor at $EDITOR — build one or set EDITOR_BIN" >&2
	exit 1
fi

# The project's export presets point at ../../../../bin/*.zip, relative to the
# project, so no per-machine path fixing is needed. Fail early and clearly if
# the template a preset wants is missing, rather than letting the export print
# a less obvious error.
need_template() {
	if [[ ! -f "$ROOT/bin/$1" ]]; then
		echo "missing template bin/$1" >&2
		return 1
	fi
}
TEMPLATE_OVERRIDE="${RTC_TEMPLATE_ZIP:-}"
PRESETS="$PROJ/export_presets.cfg"

if [[ -n "$TEMPLATE_OVERRIDE" ]]; then
	if [[ ! -f "$TEMPLATE_OVERRIDE" ]]; then
		echo "RTC_TEMPLATE_ZIP=$TEMPLATE_OVERRIDE does not exist" >&2
		exit 1
	fi
	TEMPLATE_OVERRIDE="$(cd "$(dirname "$TEMPLATE_OVERRIDE")" && pwd)/$(basename "$TEMPLATE_OVERRIDE")"
	EXT_SUPPORT=false
	[[ "$TEMPLATE_OVERRIDE" == *.dlink.zip ]] && EXT_SUPPORT=true
	echo "overriding template: $TEMPLATE_OVERRIDE (extensions_support=$EXT_SUPPORT)"
	# Restored on exit, including on failure: this file is checked in, and a
	# half-finished run must not leave a machine-specific path in it.
	cp "$PRESETS" "$PRESETS.ci-backup"
	trap 'mv -f "$PRESETS.ci-backup" "$PRESETS"' EXIT
	sed -i -E \
		-e "s#^(custom_template/(debug|release))=.*#\1=\"$TEMPLATE_OVERRIDE\"#" \
		-e "s#^variant/extensions_support=.*#variant/extensions_support=$EXT_SUPPORT#" \
		"$PRESETS"
	# The sed above rewrites every preset's template, so the threaded and dlink
	# presets would now be duplicates of the override. Only export one.
	RTC_THREADS=no
	RTC_DLINK=no
else
	need_template "godot.web.template_release.wasm32.nothreads.zip" || exit 1
fi

# BAKE=1: export only the nothreads fixture, with the shader baker, into exports/loopback_baked.
# --headless skips the baker, so this runs under xvfb-run with a real rendering driver (TASKS.md 2.29).
# Exit 2 = SKIP when xvfb-run is absent.
if [[ "${BAKE:-0}" == 1 ]]; then
	command -v xvfb-run >/dev/null || { echo "SKIP -- xvfb-run not installed; cannot bake"; exit 2; }
	"$EDITOR" --headless --path "$PROJ" --import >/dev/null 2>&1 || true
	mkdir -p "$TIER/exports/loopback_baked"
	echo "exporting nothreads (baked) -> $TIER/exports/loopback_baked"
	xvfb-run -a "$EDITOR" --rendering-driver vulkan --path "$PROJ" \
		--export-release "WebGPU" "$TIER/exports/loopback_baked/index.html" >/dev/null 2>&1 || exit 1
	[[ -f "$TIER/exports/loopback_baked/index.html" ]] || { echo "baked export produced no index.html" >&2; exit 1; }
	exit 0
fi

# First scan, separately and ignoring the exit code: with the webrtc-native
# extension present the editor aborts (exit 134) the first time it scans a
# project (F6, an upstream webrtc-native bug). It still writes
# .godot/extension_list.cfg, and every later run exits cleanly — but as a
# single step it would fail CI for a reason that has nothing to do with the
# tests. See webgpu_notes/TASKS.md §4.20 F6.
"$EDITOR" --headless --path "$PROJ" --import >/dev/null 2>&1 || true

# Godot does not create the export directory, and a missing one fails the
# export with no clear message -- which is what a cleaned exports/ or a fresh
# checkout gives you.
mkdir -p "$TIER/exports/loopback" "$TIER/exports/loopback_threads" \
	"$TIER/exports/loopback_dlink" "$TIER/exports/loopback_threads_dlink"

rc=0
echo "exporting nothreads -> $TIER/exports/loopback"
"$EDITOR" --headless --path "$PROJ" \
	--export-release "WebGPU" "$TIER/exports/loopback/index.html" >/dev/null 2>&1 || rc=1
if [[ ! -f "$TIER/exports/loopback/index.html" ]]; then
	echo "nothreads export produced no index.html" >&2
	rc=1
fi

if [[ "${RTC_THREADS:-yes}" == "yes" ]] && [[ -f "$ROOT/bin/godot.web.template_release.wasm32.zip" ]]; then
	echo "exporting threads -> $TIER/exports/loopback_threads"
	"$EDITOR" --headless --path "$PROJ" \
		--export-release "WebGPU-Threads" "$TIER/exports/loopback_threads/index.html" >/dev/null 2>&1 || rc=1
	if [[ ! -f "$TIER/exports/loopback_threads/index.html" ]]; then
		echo "threads export produced no index.html" >&2
		rc=1
	fi
else
	echo "skipping threaded export (no bin/godot.web.template_release.wasm32.zip, or RTC_THREADS=no)"
fi

# dlink (GDExtension support). The fixture project *has* a GDExtension -- the
# webrtc-native reference peer -- and it is excluded on web by its own
# exclude_tags, so this checks that a dlink export with an excluded extension
# present still boots and that WebRTC behaves identically through the side
# module. Needs the misc/emsdk_patches/ toolchain patches; see CLAUDE.md.
if [[ "${RTC_DLINK:-yes}" == "yes" ]] && [[ -f "$ROOT/bin/godot.web.template_release.wasm32.nothreads.dlink.zip" ]]; then
	echo "exporting dlink -> $TIER/exports/loopback_dlink"
	"$EDITOR" --headless --path "$PROJ" \
		--export-release "WebGPU-Dlink" "$TIER/exports/loopback_dlink/index.html" >/dev/null 2>&1 || rc=1
	for f in index.html index.side.wasm; do
		if [[ ! -f "$TIER/exports/loopback_dlink/$f" ]]; then
			echo "dlink export produced no $f" >&2
			rc=1
		fi
	done
else
	echo "skipping dlink export (no bin/godot.web.template_release.wasm32.nothreads.dlink.zip, or RTC_DLINK=no)"
fi

# threads + dlink: a side module loading on a pthread worker, which neither export
# above has. Unpatched Emscripten 6.0.9 never boots this one (TASKS §4.111).
if [[ "${RTC_DLINK:-yes}" == "yes" ]] && [[ -f "$ROOT/bin/godot.web.template_release.wasm32.dlink.zip" ]]; then
	echo "exporting threads + dlink -> $TIER/exports/loopback_threads_dlink"
	"$EDITOR" --headless --path "$PROJ" \
		--export-release "WebGPU-Threads-Dlink" "$TIER/exports/loopback_threads_dlink/index.html" >/dev/null 2>&1 || rc=1
	for f in index.html index.side.wasm; do
		if [[ ! -f "$TIER/exports/loopback_threads_dlink/$f" ]]; then
			echo "threads + dlink export produced no $f" >&2
			rc=1
		fi
	done
else
	echo "skipping threads + dlink export (no bin/godot.web.template_release.wasm32.dlink.zip, or RTC_DLINK=no)"
fi

exit $rc
