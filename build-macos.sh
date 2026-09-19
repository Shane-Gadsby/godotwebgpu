#!/usr/bin/env bash
# build-macos.sh — Native macOS build script for Godot WebGPU.
#
# Installs prerequisites (Xcode Command Line Tools, Homebrew, Python, SCons,
# Vulkan SDK/MoltenVK, ANGLE, AccessKit, Emscripten) then builds the editor,
# the native macOS export template, and the web/WebGPU export template.
# The macOS template is built universal (x86_64 + arm64) since SCons has no
# single "universal" arch value -- both arches are built separately and
# lipo'd/bundled together, mirroring this repo's own CI
# (.github/workflows/macos_builds.yml) and
# platform/macos/platform_macos_builders.py's generate_bundle step.
#
# Run this FROM the repo root, ON a real Mac. This is not for cross-compiling
# from Linux -- see build.sh (options 7-9) for that (which this script's
# macOS-native path avoids several workarounds needed there: real Vulkan/
# MoltenVK works, real codesign works, no osxcross/clang-version juggling).
#
# Usage: ./build-macos.sh [--skip-install] [--skip-web] [--jobs N]
#
# Env overrides:
#   EMSDK_DIR=path       Emscripten SDK location (default: ~/emsdk)
#   EMSDK_VERSION=x.y.z  Emscripten version to install/activate (default: 4.0.11)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

if [[ "$(uname -s)" != "Darwin" ]]; then
	echo "This script is for running natively on macOS. Use build.sh for cross-compiling macOS targets from Linux." >&2
	exit 1
fi

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
NC='\033[0m'

SKIP_INSTALL=0
SKIP_WEB=0
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
EMSDK_DIR="${EMSDK_DIR:-$HOME/emsdk}"
EMSDK_VERSION="${EMSDK_VERSION:-4.0.11}"

while [[ $# -gt 0 ]]; do
	case "$1" in
		--skip-install)
			SKIP_INSTALL=1
			shift
			;;
		--skip-web)
			SKIP_WEB=1
			shift
			;;
		--jobs)
			JOBS="$2"
			shift 2
			;;
		*)
			echo "Unknown argument: $1" >&2
			exit 1
			;;
	esac
done

step() { echo -e "\n${BOLD}==> $1${NC}"; }
ok() { echo -e "${GREEN}$1${NC}"; }
warn() { echo -e "${YELLOW}$1${NC}"; }
die() {
	echo -e "${RED}$1${NC}" >&2
	exit 1
}

# --- Prerequisites ---------------------------------------------------------

install_prereqs() {
	step "Checking for Xcode Command Line Tools..."
	if ! xcode-select -p > /dev/null 2>&1; then
		warn "Xcode Command Line Tools not found. Triggering the installer --"
		warn "a GUI dialog will appear; click through it, then this script will"
		warn "keep polling and continue automatically once it's done."
		xcode-select --install 2> /dev/null || true
		local waited=0
		until xcode-select -p > /dev/null 2>&1; do
			sleep 5
			waited=$((waited + 5))
			if [[ "$waited" -ge 1800 ]]; then
				die "Still waiting on Xcode Command Line Tools after 30 minutes. Finish the install (or run 'xcode-select --install' yourself) and re-run this script."
			fi
		done
	fi
	ok "Xcode Command Line Tools OK."

	step "Checking for Homebrew..."
	if ! command -v brew > /dev/null 2>&1; then
		warn "Homebrew not found, installing..."
		NONINTERACTIVE=1 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
		if [[ "$(uname -m)" == "arm64" ]]; then
			eval "$(/opt/homebrew/bin/brew shellenv)"
		else
			eval "$(/usr/local/bin/brew shellenv)"
		fi
	fi
	ok "Homebrew OK: $(brew --version | head -1)"

	step "Installing jq (needed by install_vulkan_sdk_macos.sh)..."
	brew list jq > /dev/null 2>&1 || brew install jq

	step "Checking for Python..."
	if ! command -v python3 > /dev/null 2>&1; then
		brew install python3
	fi
	ok "Python OK: $(python3 --version)"

	step "Installing SCons..."
	python3 -m pip install --upgrade pip --quiet --break-system-packages 2> /dev/null \
		|| python3 -m pip install --upgrade pip --quiet
	python3 -m pip install "scons==4.10.1" --quiet --break-system-packages 2> /dev/null \
		|| python3 -m pip install "scons==4.10.1" --quiet
	command -v scons > /dev/null 2>&1 || die "scons installed via pip but isn't on PATH. Add Python's user-site bin directory to PATH and re-run."
	scons --version

	if [[ -d "$REPO_ROOT/.git" ]]; then
		step "Updating git submodules..."
		git submodule update --init --recursive
	fi

	step "Fetching pre-built ANGLE..."
	python3 misc/scripts/install_angle.py || { warn "ANGLE install failed, building with angle=no."; ANGLE_ENABLED="no"; }

	step "Fetching pre-built AccessKit..."
	python3 misc/scripts/install_accesskit.py || { warn "AccessKit install failed, building with accesskit=no."; ACCESSKIT_ENABLED="no"; }

	step "Fetching Vulkan SDK / MoltenVK..."
	sh misc/scripts/install_vulkan_sdk_macos.sh || { warn "Vulkan SDK install failed, building with vulkan=no."; VULKAN_ENABLED="no"; }

	if [[ "$SKIP_WEB" -eq 0 ]]; then
		install_emsdk
	fi
}

install_emsdk() {
	step "Setting up Emscripten SDK ($EMSDK_VERSION) at $EMSDK_DIR..."
	if [[ ! -d "$EMSDK_DIR" ]]; then
		git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
	else
		(cd "$EMSDK_DIR" && git pull --ff-only) || warn "Could not update existing emsdk checkout at $EMSDK_DIR, using it as-is."
	fi
	(cd "$EMSDK_DIR" && ./emsdk install "$EMSDK_VERSION" && ./emsdk activate "$EMSDK_VERSION") \
		|| { warn "emsdk install/activate failed -- skipping the web/WebGPU template build."; SKIP_WEB=1; }
}

ANGLE_ENABLED="yes"
ACCESSKIT_ENABLED="yes"
VULKAN_ENABLED="yes"

if [[ "$SKIP_INSTALL" -eq 0 ]]; then
	install_prereqs
else
	warn "Skipping dependency installation (--skip-install). Assuming Xcode/Homebrew/SCons/Vulkan/ANGLE/AccessKit/Emscripten are already set up."
	if [[ "$SKIP_WEB" -eq 0 && ! -f "$EMSDK_DIR/emsdk_env.sh" ]]; then
		die "Emscripten not found at $EMSDK_DIR (set EMSDK_DIR, or pass --skip-web to skip the web template build)."
	fi
fi

# --- Version string + output dir -------------------------------------------

get_version() {
	python3 - "$REPO_ROOT/version.py" << 'PYEOF'
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
# (templates/, templates_linux/, templates_windows/, templates_macos/ --
# useful if this is a shared builds/ dir with output from build.sh's
# Windows/macOS cross-builds too) into a single .tpz -- the format Godot's
# Export Template Manager (editor/export/export_template_manager.cpp's
# _tpz_file_selected) expects: a zip containing one "templates/" directory
# holding version.txt (the raw version string) alongside every template
# file, flattened.
package_tpz() {
	if ! command -v zip > /dev/null 2>&1; then
		warn "'zip' not found -- skipping .tpz packaging."
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
	ok "Export template package -> $tpz"
}

step "Godot WebGPU macOS build -- version $VERSION, -j$JOBS (vulkan=$VULKAN_ENABLED angle=$ANGLE_ENABLED accesskit=$ACCESSKIT_ENABLED)"

# --- Build -------------------------------------------------------------------

build_arches() {
	# Builds the given target for both x86_64 and arm64 (SCons has no
	# "universal" arch value -- see platform_methods.py's lipo()).
	local target="$1"
	for arch in x86_64 arm64; do
		step "Building $target (arch=$arch)..."
		scons platform=macos target="$target" arch="$arch" \
			vulkan="$VULKAN_ENABLED" angle="$ANGLE_ENABLED" accesskit="$ACCESSKIT_ENABLED" \
			-j"$JOBS"
	done
}

step "Building tint_convert_cli (native SPIR-V -> WGSL precompile tool)..."
# Built explicitly, and before the editor, so it's available to copy into
# the editor .app bundle right after bundling below. (It would otherwise get
# built anyway as a side effect of the web/WebGPU template build further
# down -- wgsl_precompile.py's build_wgsl_precompiled() always invokes this
# same script -- but that happens too late, and SKIP_WEB would skip it
# entirely, and this is a fast, incremental script regardless.)
./drivers/webgpu/tint_cli/build.sh

step "Building editor..."
# webgpu=yes is required (even though this isn't a web build) for the
# editor's own export-time shader baker to actually bake anything --
# WEBGPU_SHADER_BAKER_ENABLED is gated on `env["webgpu"] and
# env.editor_build` (platform/macos/detect.py). Passed here rather than
# inside build_arches() itself since that function is shared with the
# template builds below, which don't need or benefit from it.
WEBGPU_EDITOR_FLAGS="webgpu=yes"
for arch in x86_64 arm64; do
	step "Building editor (arch=$arch)..."
	scons platform=macos target=editor arch="$arch" \
		vulkan="$VULKAN_ENABLED" angle="$ANGLE_ENABLED" accesskit="$ACCESSKIT_ENABLED" \
		$WEBGPU_EDITOR_FLAGS -j"$JOBS"
done
step "Bundling universal editor .app..."
scons platform=macos target=editor arch=x86_64 \
	vulkan="$VULKAN_ENABLED" angle="$ANGLE_ENABLED" accesskit="$ACCESSKIT_ENABLED" \
	$WEBGPU_EDITOR_FLAGS generate_bundle=yes -j"$JOBS"

dest="$OUT_DIR/editor_macos"
mkdir -p "$dest"
app="$(ls -td bin/*.app 2> /dev/null | head -1 || true)"
[[ -n "$app" ]] || die "macOS editor build succeeded but no .app bundle was found in bin/."
rm -rf "$dest/$(basename "$app")"
cp -R "$app" "$dest/"
ok "Editor -> $dest/$(basename "$app")"

# The editor's shader baker (wgsl_bake_subprocess.cpp's
# _find_tint_convert_cli()) looks for tint_convert_cli next to the RUNNING
# editor executable's own path -- for a macOS .app bundle, that's
# Contents/MacOS/ (where platform_macos_builders.py's generate_bundle()
# places the "Godot" binary itself), not the .app bundle's top level.
cp bin/tint_convert_cli "$dest/$(basename "$app")/Contents/MacOS/"
ok "tint_convert_cli -> $dest/$(basename "$app")/Contents/MacOS/"

step "Building templates (debug)..."
build_arches template_debug
step "Building templates (release)..."
build_arches template_release
step "Bundling universal templates into macos.zip..."
scons platform=macos target=template_release arch=x86_64 \
	vulkan="$VULKAN_ENABLED" angle="$ANGLE_ENABLED" accesskit="$ACCESSKIT_ENABLED" \
	generate_bundle=yes -j"$JOBS"

dest="$OUT_DIR/templates_macos"
mkdir -p "$dest"
zip="$(ls -t bin/godot_macos*.zip 2> /dev/null | head -1 || true)"
[[ -n "$zip" ]] || die "macOS template build succeeded but a .zip wasn't found in bin/."
cp "$zip" "$dest/macos.zip"
ok "Templates -> $dest"

if [[ "$SKIP_WEB" -eq 0 ]]; then
	# shellcheck source=/dev/null
	source "$EMSDK_DIR/emsdk_env.sh" > /dev/null

	step "Building web/WebGPU export template (debug)..."
	scons platform=web target=template_debug dlink_enabled=yes webgpu=yes opengl3=no threads=no -j"$JOBS"

	step "Building web/WebGPU export template (release)..."
	scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j"$JOBS"

	dest="$OUT_DIR/templates"
	mkdir -p "$dest"
	debug_zip="$(ls -t bin/godot.web.template_debug*.zip 2> /dev/null | head -1 || true)"
	release_zip="$(ls -t bin/godot.web.template_release*.zip 2> /dev/null | head -1 || true)"
	[[ -n "$debug_zip" && -n "$release_zip" ]] || die "Web template build succeeded but a .zip wasn't found in bin/."
	cp "$debug_zip" "$dest/web_nothreads_debug.zip"
	cp "$release_zip" "$dest/web_nothreads_release.zip"
	ok "Web templates -> $dest"
else
	warn "Skipping web/WebGPU template build (--skip-web)."
fi

package_tpz

echo
ok "Done. Output in builds/$VERSION/"
