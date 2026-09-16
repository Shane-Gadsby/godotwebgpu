#!/usr/bin/env bash
# build-linux.sh — Native Linux build script for Godot WebGPU.
#
# Installs prerequisites (apt build dependencies, Python, SCons, AccessKit,
# Emscripten) then builds the editor, the native Linux export templates
# (debug/release), and the full web/WebGPU export template matrix --
# GDExtension/dlink support x thread support x debug/release, 8 builds --
# so every combination selectable in the Web export preset's "GDExtension
# Support" / "Thread Support" checkboxes has a matching template. This is
# the full set needed to both develop with this fork and export the WebGPU
# target it exists for, including GDExtensions (e.g. TressFX) that need
# thread support. Mirrors this repo's own CI (.github/workflows/linux_builds.yml,
# web_builds.yml) and the install-then-build shape of build-macos.sh /
# build-windows.ps1, but builds more template variants than CI does.
#
# Note: this fork's documented/tested WebGPU configuration is threads=no
# (see CLAUDE.md); the threads=yes variants build here for completeness and
# GDExtension compatibility but aren't part of the fork's regularly-tested
# path. If a threaded web export misbehaves specifically under the WebGPU
# driver, narrow it down against a threads=no build first.
#
# Run this FROM the repo root, ON real Linux (or WSL). This is not for
# cross-compiling Windows/macOS targets -- see build.sh (options 4-9) for
# that.
#
# Usage: ./build-linux.sh [--skip-install] [--skip-web] [--skip-tint-cli] [--jobs N]
#
# Env overrides:
#   EMSDK_DIR=path       Emscripten SDK location (default: ~/emsdk)
#   EMSDK_VERSION=x.y.z  Emscripten version to install/activate (default: 4.0.11)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

if [[ "$(uname -s)" != "Linux" ]]; then
	echo "This script is for running natively on Linux. Use build.sh for cross-compiling other targets." >&2
	exit 1
fi

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
NC='\033[0m'

SKIP_INSTALL=0
SKIP_WEB=0
SKIP_TINT_CLI=0
JOBS="$(nproc 2>/dev/null || echo 4)"
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
		--skip-tint-cli)
			SKIP_TINT_CLI=1
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

# Build-time dependencies for platform=linuxbsd. Most of Godot's optional
# Linux integrations (alsa, pulse, dbus, speechd, fontconfig, udev, X11) are
# loaded via dlopen at runtime (use_sowrap=yes, the default) rather than
# linked, but the *-dev packages are still needed at compile time for their
# headers -- none of them are vendored under thirdparty/ (unlike X11/wayland's
# wire-protocol headers, which are). libvulkan-dev + mesa-vulkan-drivers cover
# the Vulkan renderer (no separate SDK install script needed on Linux, unlike
# macOS's MoltenVK). libwayland-bin provides wayland-scanner, needed to
# generate bindings from the vendored thirdparty/wayland-protocols.
APT_PACKAGES=(
	build-essential
	pkg-config
	git
	curl
	ca-certificates
	zip
	python3
	python3-pip
	python3-venv
	libx11-dev
	libxcursor-dev
	libxinerama-dev
	libxrandr-dev
	libxi-dev
	libxext-dev
	libxrender-dev
	libxfixes-dev
	libxdamage-dev
	libxkbcommon-dev
	libgl1-mesa-dev
	libglu1-mesa-dev
	libasound2-dev
	libpulse-dev
	libudev-dev
	libdbus-1-dev
	libspeechd-dev
	libfontconfig1-dev
	libwayland-dev
	libwayland-bin
	libdecor-0-dev
	libvulkan-dev
	mesa-vulkan-drivers
)

install_prereqs() {
	step "Installing apt build dependencies..."
	if ! command -v apt-get > /dev/null 2>&1; then
		warn "apt-get not found (not a Debian/Ubuntu system) -- skipping automatic package install."
		warn "Install the equivalent of: ${APT_PACKAGES[*]}"
	else
		local sudo_cmd=()
		if [[ "$(id -u)" -ne 0 ]]; then
			command -v sudo > /dev/null 2>&1 || die "Not running as root and sudo isn't available -- can't install apt packages."
			sudo_cmd=(sudo)
		fi
		"${sudo_cmd[@]}" apt-get update
		if ! "${sudo_cmd[@]}" apt-get install -y "${APT_PACKAGES[@]}"; then
			warn "Some apt packages failed to install (package names drift across distro versions,"
			warn "e.g. libasound2-dev -> libasound2t64-dev on newer Ubuntu) -- continuing; the build"
			warn "itself will fail loudly and specifically if something required is actually missing."
		fi
	fi
	ok "System packages OK."

	step "Checking for Python..."
	command -v python3 > /dev/null 2>&1 || die "python3 not found even after package install."
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

	step "Fetching pre-built AccessKit..."
	python3 misc/scripts/install_accesskit.py || { warn "AccessKit install failed, building with accesskit=no."; ACCESSKIT_ENABLED="no"; }

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

ACCESSKIT_ENABLED="yes"

if [[ "$SKIP_INSTALL" -eq 0 ]]; then
	install_prereqs
else
	warn "Skipping dependency installation (--skip-install). Assuming apt packages/SCons/AccessKit/Emscripten are already set up."
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

	local templates_out_dir="/mnt/109313D2109313D2/godot-editors/templates"
	if [[ -d "$templates_out_dir" ]]; then
		cp "$tpz" "$templates_out_dir/"
		ok "Export template package -> $templates_out_dir/$(basename "$tpz")"
	else
		warn "$templates_out_dir not found -- skipping .tpz copy there."
	fi
}

step "Godot WebGPU Linux build -- version $VERSION, -j$JOBS (accesskit=$ACCESSKIT_ENABLED)"

# --- Build -------------------------------------------------------------------

step "Building editor (platform=linuxbsd)..."
scons platform=linuxbsd target=editor accesskit="$ACCESSKIT_ENABLED" -j"$JOBS"

dest="$OUT_DIR/editor"
mkdir -p "$dest"
found=0
for f in bin/godot.linuxbsd.editor*; do
	[[ -f "$f" ]] || continue
	cp "$f" "$dest/"
	found=1
done
[[ "$found" -eq 1 ]] || die "Editor build succeeded but no output binary was found in bin/."
ok "Editor -> $dest"

EDITORS_DIR="/mnt/109313D2109313D2/godot-editors/editors"
if [[ -d "$EDITORS_DIR" ]]; then
	for f in bin/godot.linuxbsd.editor*; do
		[[ -f "$f" ]] || continue
		cp "$f" "$EDITORS_DIR/"
	done
	ok "Editor -> $EDITORS_DIR"
else
	warn "$EDITORS_DIR not found -- skipping editor copy there."
fi

step "Building Linux export template (debug)..."
scons platform=linuxbsd target=template_debug accesskit="$ACCESSKIT_ENABLED" -j"$JOBS"

step "Building Linux export template (release)..."
scons platform=linuxbsd target=template_release accesskit="$ACCESSKIT_ENABLED" -j"$JOBS"

dest="$OUT_DIR/templates_linux"
mkdir -p "$dest"
dbg="bin/godot.linuxbsd.template_debug.x86_64"
rel="bin/godot.linuxbsd.template_release.x86_64"
[[ -f "$dbg" && -f "$rel" ]] || die "Linux template build succeeded but a binary wasn't found in bin/."
# Renamed to match EditorExportPlatformLinuxBSD::get_template_file_name()
# ("linux_" + target + "." + arch) so these drop straight into
# ~/.local/share/godot/export_templates/$VERSION/.
cp "$dbg" "$dest/linux_debug.x86_64"
cp "$rel" "$dest/linux_release.x86_64"
ok "Linux templates -> $dest"

if [[ "$SKIP_WEB" -eq 0 ]]; then
	# shellcheck source=/dev/null
	source "$EMSDK_DIR/emsdk_env.sh" > /dev/null

	dest="$OUT_DIR/templates"
	mkdir -p "$dest"

	# Full web template matrix: GDExtension/dlink support x thread support x
	# debug/release (8 builds), so every combination the Web export preset's
	# "GDExtension Support" / "Thread Support" checkboxes can select has a
	# matching template. Filenames on both sides (scons output and the
	# templates/ dir) must match Godot's own naming rules exactly:
	#   - scons suffix order: .wasm32[.nothreads][.dlink]
	#     (SConstruct's ".nothreads" is appended before detect.py's ".dlink")
	#   - export_plugin.h's _get_template_name(): "web" + ["_dlink"] +
	#     ["_nothreads"] + "_debug.zip"/"_release.zip"
	for dlink in yes no; do
		for threads in yes no; do
			for target in template_debug template_release; do
				debug_label="debug"
				[[ "$target" == "template_release" ]] && debug_label="release"

				step "Building web/WebGPU export template ($debug_label, dlink=$dlink, threads=$threads)..."
				scons platform=web target="$target" dlink_enabled="$dlink" webgpu=yes opengl3=no threads="$threads" -j"$JOBS"

				scons_zip="bin/godot.web.${target}.wasm32"
				[[ "$threads" == "no" ]] && scons_zip+=".nothreads"
				[[ "$dlink" == "yes" ]] && scons_zip+=".dlink"
				scons_zip+=".zip"
				[[ -f "$scons_zip" ]] || die "Web template build succeeded but $scons_zip wasn't found."

				dest_name="web"
				[[ "$dlink" == "yes" ]] && dest_name+="_dlink"
				[[ "$threads" == "no" ]] && dest_name+="_nothreads"
				dest_name+="_${debug_label}.zip"

				cp "$scons_zip" "$dest/$dest_name"
				ok "Web template -> $dest/$dest_name"
			done
		done
	done
else
	warn "Skipping web/WebGPU template build (--skip-web)."
fi

if [[ "$SKIP_TINT_CLI" -eq 0 ]]; then
	step "Building tint_convert_cli (native SPIR-V -> WGSL precompile tool)..."
	./drivers/webgpu/tint_cli/build.sh
	ok "tint_convert_cli -> bin/tint_convert_cli"
else
	warn "Skipping tint_convert_cli build (--skip-tint-cli)."
fi

package_tpz

echo
ok "Done. Output in builds/$VERSION/"
