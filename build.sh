#!/usr/bin/env bash
# build.sh — Interactive build menu for Godot WebGPU.
#
# Builds the editor and/or export templates for the host platform, web
# (WebGPU), Windows, and macOS, and copies the resulting artifacts into
# builds/<version>/<target>/.
#
# Windows and macOS builds are cross-compiled from Linux (or from macOS, for
# Windows) and require extra toolchains:
#   - Windows: mingw-w64 (x86_64-w64-mingw32-gcc on PATH)
#   - macOS:   osxcross (OSXCROSS_ROOT set, its bin/ on PATH) — not needed
#              when already running on macOS natively
#
# Templates (option 2/3/10) always rebuild bin/tint_convert_cli first and
# force-delete drivers/webgpu/wgsl_precompiled.gen.h before the SCons web
# build. Both are necessary for "latest changes" to actually reach the
# output: wgsl_precompile.py (run by every `webgpu=yes` SCons build) shells
# out to tint_convert_cli to precompile every built-in engine shader's WGSL,
# but SCons's own Depends() for the generated table it produces only names
# wgsl_precompile.py itself — not tint_convert_cli's binary, spirv_preprocess.cpp,
# tint_wrapper.cpp, or any vendored thirdparty/tint source — so an incremental
# build would otherwise silently keep serving stale WGSL from whatever
# tint_convert_cli last happened to produce.
#
# Usage: ./build.sh
#
# Env overrides:
#   JOBS=N            parallel build jobs (default: nproc)
#   EMSDK_DIR=path    Emscripten SDK location (default: ~/emsdk)
#   OSXCROSS_ROOT=path  osxcross install location (default: ~/osxcross;
#                       required for macOS cross-builds when not running
#                       natively on macOS)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
EMSDK_DIR="${EMSDK_DIR:-$HOME/emsdk}"

case "$(uname -s)" in
	Linux) HOST_PLATFORM="linuxbsd" ;;
	Darwin) HOST_PLATFORM="macos" ;;
	*)
		echo "Unsupported host platform: $(uname -s)" >&2
		exit 1
		;;
esac

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
NC='\033[0m'

# Derive the export-template-style version string (e.g. "4.7.2.stable",
# or "4.7.stable" when patch is 0) from version.py, the same way Godot
# names its export_templates directories.
get_version() {
	python3 - "$REPO_ROOT/version.py" <<'PYEOF'
import sys
ns = {}
with open(sys.argv[1]) as f:
    exec(f.read(), ns)
parts = [str(ns["major"]), str(ns["minor"])]
if ns.get("patch"):
    parts.append(str(ns["patch"]))
print(".".join(parts) + "." + ns["status"])
PYEOF
}

VERSION="$(get_version)"
OUT_DIR="$REPO_ROOT/builds/$VERSION"

# Packages whatever template dirs currently exist under $OUT_DIR
# (templates/, templates_linux/, templates_windows/, templates_macos/) into a
# single .tpz -- the format Godot's Export Template Manager
# (editor/export/export_template_manager.cpp's _tpz_file_selected) expects:
# a zip containing one "templates/" directory holding version.txt (the raw
# version string) alongside every template file, flattened. Re-running this
# after building more platforms/targets just repackages the current state,
# so it's safe to call after each individual template build. No-ops if no
# template dir exists yet.
package_tpz() {
	if ! command -v zip > /dev/null 2>&1; then
		echo -e "${YELLOW}'zip' not found -- skipping .tpz packaging (install it, e.g. 'apt install zip' / 'brew install zip').${NC}" >&2
		return
	fi

	local staging="$OUT_DIR/.tpz_staging"
	rm -rf "$staging"
	mkdir -p "$staging/templates"

	local found=0
	local d f
	for d in "$OUT_DIR/templates" "$OUT_DIR/templates_linux" "$OUT_DIR/templates_windows" "$OUT_DIR/templates_macos"; do
		[[ -d "$d" ]] || continue
		for f in "$d"/*; do
			[[ -f "$f" ]] || continue
			cp "$f" "$staging/templates/"
			found=1
		done
	done

	if [[ "$found" -eq 0 ]]; then
		rm -rf "$staging"
		return
	fi

	echo "$VERSION" > "$staging/templates/version.txt"

	local tpz="$OUT_DIR/godot-webgpu-export-templates-$VERSION.tpz"
	rm -f "$tpz"
	(cd "$staging" && zip -rq "$tpz" templates)
	rm -rf "$staging"
	echo -e "${GREEN}Export template package -> $tpz${NC}"
}

build_editor() {
	echo -e "${BOLD}Building editor (platform=$HOST_PLATFORM)...${NC}"
	# webgpu=yes is required for the native editor's own export-time shader
	# baker to actually bake anything (WEBGPU_SHADER_BAKER_ENABLED, gated on
	# `env["webgpu"] and env.editor_build` -- see platform/*/detect.py).
	# Omitting it silently produces a working editor that exports fine but
	# never bakes any shader.
	scons platform="$HOST_PLATFORM" target=editor webgpu=yes -j"$JOBS"

	local dest="$OUT_DIR/editor"
	mkdir -p "$dest"
	local found=0
	for f in bin/godot."$HOST_PLATFORM".editor*; do
		[[ -f "$f" ]] || continue
		cp "$f" "$dest/"
		found=1
	done
	if [[ "$found" -eq 0 ]]; then
		echo -e "${RED}Editor build succeeded but no output binary was found in bin/.${NC}" >&2
		exit 1
	fi
	echo -e "${GREEN}Editor -> $dest${NC}"

	# The editor's shader baker (wgsl_bake_subprocess.cpp's
	# _find_tint_convert_cli()) looks for tint_convert_cli next to the
	# RUNNING editor executable's own path, not bin/ or $PATH -- built here
	# (host-native, safe) rather than in build_windows_editor()/
	# build_macos_editor() below, which cross-compile: tint_cli/build.sh
	# picks its platform-specific sources from `uname -s` (the HOST, always
	# Linux/macOS here, never the cross-compile target), so cross-compiling
	# it via mingw/osxcross would silently produce a broken, wrong-OS binary
	# rather than a working one -- not attempted.
	build_tint_cli
	cp bin/tint_convert_cli "$dest/"
	echo -e "${GREEN}tint_convert_cli -> $dest${NC}"
}

build_tint_cli() {
	echo -e "${BOLD}Building tint_convert_cli (native SPIR-V -> WGSL precompile tool)...${NC}"
	./drivers/webgpu/tint_cli/build.sh
	echo -e "${GREEN}tint_convert_cli -> bin/tint_convert_cli${NC}"
}

build_templates() {
	if [[ ! -f "$EMSDK_DIR/emsdk_env.sh" ]]; then
		echo -e "${RED}Emscripten not found at $EMSDK_DIR (set EMSDK_DIR to override).${NC}" >&2
		exit 1
	fi
	# shellcheck source=/dev/null
	source "$EMSDK_DIR/emsdk_env.sh" > /dev/null

	# tint_convert_cli MUST be built (and current) before this: SCons's
	# `webgpu=yes` build runs drivers/webgpu/wgsl_precompile.py, which shells
	# out to bin/tint_convert_cli to precompile every built-in engine
	# shader's WGSL ahead of time. Then force-delete the generated table
	# SCons produces from that: its own Depends() only names
	# wgsl_precompile.py itself, not tint_convert_cli's binary or any of the
	# C++/vendored-Tint sources that actually determine its output, so an
	# incremental build would otherwise silently keep serving whatever WGSL
	# a stale tint_convert_cli produced last time.
	build_tint_cli
	if [[ -f "drivers/webgpu/wgsl_precompiled.gen.h" ]]; then
		echo -e "${BOLD}Removing drivers/webgpu/wgsl_precompiled.gen.h to force a fresh precompile...${NC}"
		rm -f drivers/webgpu/wgsl_precompiled.gen.h
	fi

	echo -e "${BOLD}Building web export template (debug)...${NC}"
	scons platform=web target=template_debug dlink_enabled=yes webgpu=yes opengl3=no threads=no -j"$JOBS"

	echo -e "${BOLD}Building web export template (release)...${NC}"
	scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j"$JOBS"

	local dest="$OUT_DIR/templates"
	mkdir -p "$dest"

	local debug_zip release_zip
	debug_zip="$(ls -t bin/godot.web.template_debug*.zip 2> /dev/null | head -1 || true)"
	release_zip="$(ls -t bin/godot.web.template_release*.zip 2> /dev/null | head -1 || true)"

	if [[ -z "$debug_zip" || -z "$release_zip" ]]; then
		echo -e "${RED}Template build succeeded but a .zip wasn't found in bin/.${NC}" >&2
		exit 1
	fi

	# Renamed to match the filenames Godot's export_templates directory expects,
	# so these can be dropped straight in:
	#   ~/.local/share/godot/export_templates/$VERSION/  (Linux)
	#   ~/Library/Application Support/Godot/export_templates/$VERSION/  (macOS)
	cp "$debug_zip" "$dest/web_nothreads_debug.zip"
	cp "$release_zip" "$dest/web_nothreads_release.zip"
	echo -e "${GREEN}Templates -> $dest${NC}"
	package_tpz
}

check_mingw() {
	if ! command -v x86_64-w64-mingw32-gcc > /dev/null 2>&1; then
		echo -e "${RED}MinGW-w64 not found (expected x86_64-w64-mingw32-gcc on PATH).${NC}" >&2
		echo -e "${RED}Install it (e.g. 'apt install mingw-w64' or 'brew install mingw-w64') to cross-build for Windows.${NC}" >&2
		exit 1
	fi
	# Thin archives break at link time when cross-compiling from Linux with
	# this mingw-w64/binutils combo ("error opening thin archive member") —
	# see the GODOT_MINGW_NO_THIN_AR guard in platform/windows/detect.py.
	export GODOT_MINGW_NO_THIN_AR=1
}

check_osxcross() {
	if [[ "$HOST_PLATFORM" == "macos" ]]; then
		return
	fi
	if [[ -z "${OSXCROSS_ROOT:-}" ]] && [[ -d "$HOME/osxcross/target/bin" ]]; then
		export OSXCROSS_ROOT="$HOME/osxcross"
	fi
	if [[ -n "${OSXCROSS_ROOT:-}" ]]; then
		export PATH="$OSXCROSS_ROOT/target/bin:$PATH"
	fi
	local clang_bin
	clang_bin="$(compgen -G "${OSXCROSS_ROOT:-/nonexistent}/target/bin/x86_64-apple-darwin*-clang" | head -1 || true)"
	if [[ -z "$clang_bin" ]]; then
		echo -e "${RED}osxcross not found (expected OSXCROSS_ROOT set, with a built x86_64-apple-darwin*-clang in its target/bin/).${NC}" >&2
		echo -e "${RED}See https://docs.godotengine.org/en/stable/contributing/development/compiling/compiling_for_macos.html for cross-compiling setup, or run this on a Mac.${NC}" >&2
		exit 1
	fi

	# platform/macos/detect.py builds the cross-compiler name from an
	# "osxcross_sdk" scons var (default: the stale placeholder "darwin16"),
	# not from whatever osxcross actually built — so it must be passed
	# explicitly to match, e.g. "darwin24.5" for x86_64-apple-darwin24.5-clang.
	OSXCROSS_SDK_VER="$(basename "$clang_bin" | sed -E 's/^x86_64-apple-(darwin[0-9.]+)-clang$/\1/')"

	# The osxcross wrapper binaries (e.g. arm64-apple-darwinNN-clang++) exec
	# the literal unversioned "clang"/"clang++" via PATH at runtime — not
	# whatever CC/CXX osxcross itself was built with. Ubuntu's default
	# "clang" alias is often too old for recent SDK headers (e.g. the
	# `visionOS` availability platform needs clang 19+), so shim "clang"/
	# "clang++" to a known-good pinned version, ahead of PATH.
	#
	# Pinned (not "whatever's newest installed") because this version also
	# needs compiler-rt built+installed into ITS OWN resource dir (the
	# wrapper's implicit link step needs it for @available's runtime
	# check, e.g. ___isPlatformVersionAtLeast) — see README.COMPILER-RT.md.
	# Override by exporting OSXCROSS_CLANG_VER before running this script.
	local pinned_clang="${OSXCROSS_CLANG_VER:-19}"
	if ! command -v "clang-$pinned_clang" > /dev/null 2>&1; then
		echo -e "${RED}clang-$pinned_clang not found (needed as the osxcross host compiler; install it or set OSXCROSS_CLANG_VER).${NC}" >&2
		exit 1
	fi
	local shim_dir="/tmp/osxcross-clang-shim"
	mkdir -p "$shim_dir"
	ln -sf "$(command -v "clang-$pinned_clang")" "$shim_dir/clang"
	ln -sf "$(command -v "clang++-$pinned_clang")" "$shim_dir/clang++"
	export PATH="$shim_dir:$PATH"

	local resource_dir
	resource_dir="$(clang-"$pinned_clang" -print-resource-dir)"
	if [[ ! -f "$resource_dir/lib/darwin/libclang_rt.osx.a" ]]; then
		echo -e "${RED}compiler-rt isn't installed for clang-$pinned_clang ($resource_dir/lib/darwin/ is missing libclang_rt.osx.a).${NC}" >&2
		echo -e "${RED}Build it: cd \$OSXCROSS_ROOT && PATH=\"\$OSXCROSS_ROOT/target/bin:\$PATH\" ./build_compiler_rt.sh${NC}" >&2
		echo -e "${RED}then copy build/compiler-rt/compiler-rt/build/lib/darwin/* and include/sanitizer into $resource_dir/lib/darwin/ and $resource_dir/include/ (needs sudo).${NC}" >&2
		exit 1
	fi
}

build_windows_editor() {
	check_mingw
	echo -e "${BOLD}Building editor (platform=windows)...${NC}"
	# webgpu=yes needed for the shader baker to actually bake -- see build_editor()'s comment.
	scons platform=windows target=editor arch=x86_64 use_mingw=yes d3d12=no webgpu=yes -j"$JOBS"

	local dest="$OUT_DIR/editor_windows"
	mkdir -p "$dest"
	local found=0
	for f in bin/godot.windows.editor.x86_64*.exe; do
		[[ -f "$f" ]] || continue
		cp "$f" "$dest/"
		found=1
	done
	if [[ "$found" -eq 0 ]]; then
		echo -e "${RED}Windows editor build succeeded but no .exe was found in bin/.${NC}" >&2
		exit 1
	fi
	echo -e "${GREEN}Windows editor -> $dest${NC}"

	# tint_convert_cli.exe is deliberately NOT built/copied here -- see
	# build_editor()'s comment on why cross-compiling it via mingw would
	# silently produce a broken, wrong-OS binary. Shader baking will no-op
	# gracefully (unbaked shaders still work via the runtime Tint fallback)
	# for editors built by this function until that's fixed properly (needs
	# tint_cli/build.sh to pick platform sources from the target, not
	# `uname -s`) or built natively on Windows instead.
}

build_windows_templates() {
	check_mingw

	echo -e "${BOLD}Building Windows export template (debug)...${NC}"
	scons platform=windows target=template_debug arch=x86_64 use_mingw=yes d3d12=no -j"$JOBS"

	echo -e "${BOLD}Building Windows export template (release)...${NC}"
	scons platform=windows target=template_release arch=x86_64 use_mingw=yes d3d12=no -j"$JOBS"

	local dest="$OUT_DIR/templates_windows"
	mkdir -p "$dest"

	local dbg="bin/godot.windows.template_debug.x86_64.exe"
	local dbg_console="bin/godot.windows.template_debug.x86_64.console.exe"
	local rel="bin/godot.windows.template_release.x86_64.exe"
	local rel_console="bin/godot.windows.template_release.x86_64.console.exe"
	if [[ ! -f "$dbg" || ! -f "$rel" ]]; then
		echo -e "${RED}Windows template build succeeded but an .exe wasn't found in bin/.${NC}" >&2
		exit 1
	fi

	cp "$dbg" "$dest/windows_debug_x86_64.exe"
	cp "$rel" "$dest/windows_release_x86_64.exe"
	[[ -f "$dbg_console" ]] && cp "$dbg_console" "$dest/windows_debug_x86_64_console.exe"
	[[ -f "$rel_console" ]] && cp "$rel_console" "$dest/windows_release_x86_64_console.exe"
	echo -e "${GREEN}Windows templates -> $dest${NC}"
	package_tpz
}

build_macos_editor() {
	check_osxcross

	# arm64-only: Vulkan/MoltenVK needs a macOS-only .app installer we can't
	# run from Linux, and Metal (the no-extra-deps option) only supports
	# arm64 (x86_64 falls back to a non-functional renderer without it).
	# vulkan=no here avoids the MoltenVK SDK requirement entirely.
	echo -e "${BOLD}Building editor (platform=macos, arm64)...${NC}"
	# webgpu=yes needed for the shader baker to actually bake -- see build_editor()'s comment.
	scons platform=macos target=editor arch=arm64 vulkan=no osxcross_sdk="$OSXCROSS_SDK_VER" bundle_sign_identity="" generate_bundle=yes webgpu=yes -j"$JOBS"

	local dest="$OUT_DIR/editor_macos"
	mkdir -p "$dest"
	local app
	app="$(ls -td bin/*.app 2> /dev/null | head -1 || true)"
	if [[ -z "$app" ]]; then
		echo -e "${RED}macOS editor build succeeded but no .app bundle was found in bin/.${NC}" >&2
		exit 1
	fi
	rm -rf "$dest/$(basename "$app")"
	cp -R "$app" "$dest/"
	echo -e "${GREEN}macOS editor -> $dest/$(basename "$app")${NC}"

	# tint_convert_cli is deliberately NOT built/copied here -- see
	# build_editor()'s comment on why cross-compiling it via osxcross would
	# silently produce a broken, wrong-OS binary (tint_cli/build.sh picks
	# platform sources from `uname -s`, i.e. Linux, not the macOS target).
	# Shader baking will no-op gracefully (unbaked shaders still work via the
	# runtime Tint fallback) for editors built by this function until that's
	# fixed properly, or built natively via build-macos.sh instead (which
	# does build and place a real tint_convert_cli, since it runs on an
	# actual Mac).
}

build_macos_templates() {
	check_osxcross

	# arm64-only, same reasoning as build_macos_editor (vulkan=no, no x86_64).
	# generate_bundle's template packaging needs both template_debug and
	# template_release already built before it can zip them together, so
	# these must stay as two separate scons calls.
	echo -e "${BOLD}Building macOS export template (debug, arm64)...${NC}"
	scons platform=macos target=template_debug arch=arm64 vulkan=no osxcross_sdk="$OSXCROSS_SDK_VER" -j"$JOBS"

	echo -e "${BOLD}Building macOS export template (release, arm64) + bundling...${NC}"
	scons platform=macos target=template_release arch=arm64 vulkan=no osxcross_sdk="$OSXCROSS_SDK_VER" bundle_sign_identity="" generate_bundle=yes -j"$JOBS"

	local dest="$OUT_DIR/templates_macos"
	mkdir -p "$dest"

	local zip
	zip="$(ls -t bin/godot_macos*.zip 2> /dev/null | head -1 || true)"
	if [[ -z "$zip" ]]; then
		echo -e "${RED}macOS template build succeeded but a .zip wasn't found in bin/.${NC}" >&2
		exit 1
	fi

	cp "$zip" "$dest/macos.zip"
	echo -e "${GREEN}macOS templates -> $dest${NC}"
	package_tpz
}

build_all() {
	build_editor
	build_templates
	build_windows_editor
	build_windows_templates
	build_macos_editor
	build_macos_templates
}

clean_cache() {
	local size
	size="$(du -sh bin/obj .sconsign*.dblite 2> /dev/null | awk '{sum=sum" "$1} END{print sum}')"
	echo -e "${YELLOW}This deletes bin/obj/ and .sconsign*.dblite (intermediate objects + scons' dependency cache),${NC}"
	echo -e "${YELLOW}forcing a full rebuild from scratch next time. Current size:${size:- (nothing to clean)}${NC}"
	read -rp "Are you sure? [y/N]: " confirm
	if [[ "$confirm" != "y" && "$confirm" != "Y" ]]; then
		echo "Cancelled."
		return
	fi
	rm -rf bin/obj
	rm -f .sconsign*.dblite
	echo -e "${GREEN}Build cache cleared.${NC}"
}

show_menu() {
	echo
	echo -e "${BOLD}Godot WebGPU Build${NC}"
	echo "  Version: $VERSION"
	echo "  Output:  builds/$VERSION/"
	echo
	echo "  1) Editor only ($HOST_PLATFORM)"
	echo "  2) Templates only (web, debug + release -- also rebuilds tint_convert_cli first)"
	echo "  3) Editor + templates ($HOST_PLATFORM + web)"
	echo "  4) Windows: editor only"
	echo "  5) Windows: templates only (debug + release)"
	echo "  6) Windows: editor + templates"
	echo "  7) macOS: editor only (arm64)"
	echo "  8) macOS: templates only (arm64)"
	echo "  9) macOS: editor + templates"
	echo " 10) Build ALL targets (editor + templates, all platforms)"
	echo " 11) tint_convert_cli only (native SPIR-V -> WGSL precompile tool)"
	echo " 12) Clear build cache (bin/obj/ + .sconsign*.dblite)"
	echo " 13) Quit"
	echo
}

main() {
	show_menu
	read -rp "Select an option [1-13]: " choice
	case "$choice" in
		1) build_editor ;;
		2) build_templates ;;
		3)
			build_editor
			build_templates
			;;
		4) build_windows_editor ;;
		5) build_windows_templates ;;
		6)
			build_windows_editor
			build_windows_templates
			;;
		7) build_macos_editor ;;
		8) build_macos_templates ;;
		9)
			build_macos_editor
			build_macos_templates
			;;
		10) build_all ;;
		11) build_tint_cli ;;
		12)
			clean_cache
			exit 0
			;;
		13)
			echo "Bye."
			exit 0
			;;
		*)
			echo -e "${YELLOW}Invalid choice.${NC}"
			exit 1
			;;
	esac
	echo
	echo -e "${GREEN}Done. Output in builds/$VERSION/${NC}"
}

main "$@"
