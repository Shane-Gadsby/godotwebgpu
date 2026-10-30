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
# thread support. Braced GDScript (.gdb) is built into the editor and every
# template (--no-gdscript-braces builds the standard GDScript instead), and the
# editor plugins for it -- the Rider plugin and the VS Code .vsix, from the
# sibling repos ../godot-gdb-rider and ../godot-gdb-vscode -- are built at the
# end and copied to builds/<version>/editor_plugins/. Mirrors this repo's own CI (.github/workflows/linux_builds.yml,
# web_builds.yml) and the install-then-build shape of build-macos.sh /
# build-windows.ps1, but builds more template variants than CI does.
#
# Note: this fork's documented/tested WebGPU configuration is threads=no
# (see CLAUDE.md), though threads=yes dlink_enabled=no is now also fixed and
# live-verified (webgpu_notes/TASKS.md §4.5) -- WorkerThreadPool-dispatched
# shader/pipeline compilation was landing on background pthreads, which
# WebGPU's per-thread JS object model can't handle; fixed via
# API_TRAIT_REQUIRES_SYNCHRONOUS_PIPELINE_COMPILATION. threads=yes
# dlink_enabled=yes (GDExtension + threads together) used to be broken by an
# Emscripten-internal dylink+pthread initialization race; it now works, but only
# on a toolchain carrying this fork's Emscripten patch, which this script
# applies for you (misc/emsdk_patches/, webgpu_notes/TASKS.md §4.5).
# Without it that configuration does not boot (0001).
# If a threaded web export misbehaves under the WebGPU driver, narrow it down
# against a threads=no build first.
#
# Order: the editor, wgsl_convert_cli, then the no-threads, no-dlink web
# templates (debug + release) -- the pair most exports use -- installed at once
# with a "Ready to export" line, so you can export while the rest builds; then
# the Linux templates and the other six web variants (webgpu_notes/TASKS.md
# 2.50).
#
# Run this FROM the repo root, ON real Linux (or WSL). This is not for
# cross-compiling Windows/macOS targets -- see build.sh (options 4-9) for
# that.
#
# Build order matters here: wgsl_convert_cli (a separate, non-SCons native
# build -- see drivers/webgpu/wgsl_cli/build.sh) is built BEFORE the web/
# WebGPU template matrix, because drivers/webgpu/wgsl_precompile.py (which
# SCons runs as part of every `webgpu=yes` web build) shells out to it to
# precompile every built-in engine shader's WGSL ahead of time. SCons has no
# visibility into that dependency on its own, so drivers/webgpu/SCsub
# fingerprints it (every vendored SPIRV-Cross/SPIRV-Tools and wgsl_cli source's
# size+mtime, bin/wgsl_convert_cli's hash, glslangValidator's version) and
# re-bakes the table whenever any of it moves. That keeps the editor's baker and
# every template's baked WGSL in step without this script having to delete the
# generated header on every run, which is what it used to do (--force-shader-rebake
# still does, for when you want the belt and braces).
#
# Build speed. The full run is 11 SCons invocations and, by design, shares
# nothing between them: every variant has its own OBJSUFFIX, so a full matrix is
# ~33,000 compiles, and nothing short of building fewer variants (or several at
# once in separate trees) changes that number. What this script does trim:
#   - the WGSL bake is skipped when the shader pipeline hasn't changed (it used
#     to be forced every run), and runs glslangValidator and wgsl_convert_cli
#     across all cores when it does (~33s -> ~3s for 274 modules).
#   - apt/pip/AccessKit/emsdk are checked before being re-run, so a warm machine
#     spends seconds in setup rather than minutes. Note that this means `emsdk
#     install` no longer re-extracts (and so no longer un-patches) the toolchain
#     on every run.
#   - only the editor binary this run built is copied to builds/ and editors/,
#     not every godot.linuxbsd.editor* in bin/ (a dev_build editor is 1.1 GB).
#   - mold (else lld) for the three native links. Small here -- with the default
#     debug_symbols=no the editor links in ~6s with ld.bfd and ~2s with mold --
#     but free, and it is what makes a dev_build=yes editor (1.1 GB of DWARF)
#     link in seconds rather than a minute.
#   - ccache is ON by default (symlink masquerade so it also covers em++; see
#     setup_ccache), making the second compile of any translation unit a hit
#     after a branch switch, a reverted experiment, or a re-run because one
#     template failed to link. --no-ccache opts out.
#     SCons records the resolved compiler path as an implicit dependency of
#     every object, so *changing which binary compiles* costs a full recompile
#     of all 11 variants (~4 min each on 24 cores). That is why --no-ccache does
#     NOT remove the masquerade from PATH: it sets CCACHE_DISABLE=1 instead, so
#     ccache hands straight off to the real compiler and the recorded path is
#     unchanged. Opting out, and back in, is therefore free. Only the first run
#     after ccache became the default pays the one-time recompile.
#     To throw the cache away rather than bypass it, run `ccache -C`.
#   - --skip-native-templates and --web-targets/--web-threads/--web-dlink narrow
#     what gets built while iterating. The full matrix is still the default; a
#     narrowed run leaves builds/ a mix of this run's templates and an older
#     run's, which is exactly what the shader-bake fingerprint above cannot
#     protect you from.
# Each step's duration is printed at the end of the run.
#
# Usage: ./build-linux.sh [options]   (--help lists them)
#
# Env overrides:
#   EMSDK_DIR=path       Emscripten SDK location (default: ~/emsdk)
#   EMSDK_VERSION=x.y.z  Emscripten version to install/activate (default: 6.0.12)
#   CCACHE_MAXSIZE=size  ccache budget for the 11 variants (default: 50G)
#   LINKER=name          Native linker (default: auto -> mold, lld, or system)
#   USE_CCACHE=no        Same as --no-ccache (default: yes)
#   GDB_RIDER_DIR=path   Rider plugin repo (default: ../godot-gdb-rider)
#   GDB_VSCODE_DIR=path  VS Code extension repo (default: ../godot-gdb-vscode)
#   EDITORS_DIR=path     Where the editor and wgsl_convert_cli are also installed
#                        (default: /mnt/109313D2109313D2/godot-editors/editors;
#                        skipped when missing, so a nonexistent path opts out)
#   TPZ_COPY_DIR=path    Where the .tpz is also copied (default:
#                        /mnt/109313D2109313D2/godot-editors/templates; same rule)

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
SKIP_WGSL_CLI=0
SKIP_NATIVE_TEMPLATES=0
FORCE_SHADER_REBAKE=0
SKIP_EDITOR_PLUGINS=0
PLUGINS_ONLY=0
GDSCRIPT_BRACES="yes"
WEB_RAYCAST="yes"
USE_CCACHE="${USE_CCACHE:-yes}"
LINKER="${LINKER:-auto}"
JOBS="$(nproc 2>/dev/null || echo 4)"
EMSDK_DIR="${EMSDK_DIR:-$HOME/emsdk}"
EMSDK_VERSION="${EMSDK_VERSION:-6.0.12}"
SCONS_VERSION="4.10.1"
GDB_RIDER_DIR="${GDB_RIDER_DIR:-$REPO_ROOT/../godot-gdb-rider}"
GDB_VSCODE_DIR="${GDB_VSCODE_DIR:-$REPO_ROOT/../godot-gdb-vscode}"
# Installs outside this tree. Overridable so a test run from a second worktree
# does not replace the editor and templates the user exports with.
EDITORS_DIR="${EDITORS_DIR:-/mnt/109313D2109313D2/godot-editors/editors}"
TPZ_COPY_DIR="${TPZ_COPY_DIR:-/mnt/109313D2109313D2/godot-editors/templates}"
# Pinned so a run doesn't ask the npm registry for the newest vsce every time.
VSCE_VERSION="3.9.2"

# Web template matrix filters. Defaults are the full 8-build matrix; narrowing
# any of them is an iteration shortcut, not a release build (see the warning
# emitted below for why).
WEB_TARGETS="debug,release"
WEB_THREADS="yes,no"
WEB_DLINK="yes,no"

usage() {
	cat << 'EOF'
Usage: ./build-linux.sh [options]

Builds the editor, then the no-threads, no-dlink web templates (installed at
once, "Ready to export"), then the Linux templates and the rest of the web matrix.

  --skip-install            Don't touch apt/pip/AccessKit/emsdk (they are also
                            skipped automatically when already up to date).
  --skip-web                Editor + native templates only.
  --skip-native-templates   Skip the Linux export templates (editor + web only).
  --skip-wgsl-cli           Don't rebuild bin/wgsl_convert_cli (--skip-tint-cli,
                            its name before the Tint removal, still works).
  --no-gdscript-braces      Build the standard editor and templates, without
                            braced GDScript (.gdb) support (gdscript_braces=no;
                            it is on by default). Also skips the editor plugins,
                            which need a braced editor.
  --no-web-raycast          Build the web templates without the raycast module
                            (Embree: occlusion culling), module_raycast_enabled=no,
                            as upstream (GH-70621). On by default (TASKS.md 2.45);
                            --web-raycast is accepted and does nothing.
  --skip-editor-plugins     Don't build the Rider plugin or the VS Code .vsix.
  --plugins-only            Build ONLY the Rider plugin and the VS Code .vsix
                            (no prerequisites, no SCons, no templates); safe
                            while another build runs in this tree.
  --force-shader-rebake     Delete drivers/webgpu/wgsl_precompiled.gen.h first,
                            forcing the WGSL table to be re-baked even if the
                            shader pipeline is unchanged.
  --no-ccache               Bypass ccache for this run (it is on by default).
                            Costs no rebuild: the compiler path stays the same
                            and ccache is disabled via CCACHE_DISABLE, so you
                            can opt out and back in freely. To discard the
                            cache itself instead, run `ccache -C`.
  --linker NAME             default|bfd|gold|lld|mold for the native builds
                            (default: auto -- mold, else lld, else the system
                            default).
  --web-targets LIST        Comma list of debug,release   (default: both)
  --web-threads LIST        Comma list of yes,no          (default: both)
  --web-dlink LIST          Comma list of yes,no          (default: both)
  --jobs N                  Parallel jobs (default: nproc).

Env overrides: EMSDK_DIR, EMSDK_VERSION, CCACHE_MAXSIZE, LINKER, USE_CCACHE,
GDB_RIDER_DIR, GDB_VSCODE_DIR, EDITORS_DIR, TPZ_COPY_DIR.
EOF
}

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
		--skip-native-templates)
			SKIP_NATIVE_TEMPLATES=1
			shift
			;;
		--skip-wgsl-cli | --skip-tint-cli)
			SKIP_WGSL_CLI=1
			shift
			;;
		--force-shader-rebake)
			FORCE_SHADER_REBAKE=1
			shift
			;;
		--no-gdscript-braces)
			GDSCRIPT_BRACES="no"
			shift
			;;
		--web-raycast)
			WEB_RAYCAST="yes"
			shift
			;;
		--no-web-raycast)
			WEB_RAYCAST="no"
			shift
			;;
		--skip-editor-plugins)
			SKIP_EDITOR_PLUGINS=1
			shift
			;;
		--plugins-only)
			PLUGINS_ONLY=1
			shift
			;;
		--ccache)
			USE_CCACHE="yes"
			shift
			;;
		--no-ccache)
			USE_CCACHE="no"
			shift
			;;
		--linker)
			LINKER="$2"
			shift 2
			;;
		--web-targets)
			WEB_TARGETS="$2"
			shift 2
			;;
		--web-threads)
			WEB_THREADS="$2"
			shift 2
			;;
		--web-dlink)
			WEB_DLINK="$2"
			shift 2
			;;
		--jobs)
			JOBS="$2"
			shift 2
			;;
		-h | --help)
			usage
			exit 0
			;;
		*)
			echo "Unknown argument: $1" >&2
			usage >&2
			exit 1
			;;
	esac
done

if [[ "$PLUGINS_ONLY" -eq 1 && ( "$SKIP_EDITOR_PLUGINS" -eq 1 || "$GDSCRIPT_BRACES" == "no" ) ]]; then
	echo "--plugins-only builds nothing together with --skip-editor-plugins or --no-gdscript-braces." >&2
	exit 1
fi

# step() doubles as the run's stopwatch: each call closes the previous step and
# records how long it took, so the summary at the end says where a long run
# actually went instead of leaving it to guesswork.
STEP_NAMES=()
STEP_SECS=()
STEP_CURRENT=""
STEP_T0=0
# Seconds into the run at which the no-threads, no-dlink web pair was installed.
READY_AT=""

_close_step() {
	if [[ -n "$STEP_CURRENT" ]]; then
		STEP_NAMES+=("$STEP_CURRENT")
		STEP_SECS+=("$((SECONDS - STEP_T0))")
		STEP_CURRENT=""
	fi
}

step() {
	_close_step
	STEP_CURRENT="$1"
	STEP_T0="$SECONDS"
	echo -e "\n${BOLD}==> $1${NC}"
}
ok() { echo -e "${GREEN}$1${NC}"; }
warn() { echo -e "${YELLOW}$1${NC}"; }
die() {
	echo -e "${RED}$1${NC}" >&2
	exit 1
}

fmt_duration() {
	local total="$1"
	printf '%d:%02d:%02d' "$((total / 3600))" "$(((total % 3600) / 60))" "$((total % 60))"
}

print_timings() {
	_close_step
	echo -e "\n${BOLD}==> Time per step (>=2s)${NC}"
	local i
	for i in "${!STEP_NAMES[@]}"; do
		[[ "${STEP_SECS[$i]}" -ge 2 ]] || continue
		printf '  %8s  %s\n' "$(fmt_duration "${STEP_SECS[$i]}")" "${STEP_NAMES[$i]}"
	done
	printf '  %8s  %s\n' "$(fmt_duration "$SECONDS")" "TOTAL"
	if [[ -n "$READY_AT" ]]; then
		printf '  %8s  %s\n' "$(fmt_duration "$READY_AT")" "ready to export (web_nothreads pair installed)"
	fi
}

# Comma-list membership, for the web matrix filters.
list_has() {
	local needle="$1"
	local haystack=",$2,"
	[[ "$haystack" == *",$needle,"* ]]
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
	# Build accelerators, not build requirements: ccache (opt-in, --ccache) and
	# mold (used when present). If the package names drift, nothing breaks.
	ccache
	mold
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

# Everything in install_prereqs() is a no-op on a machine that has already run
# this script once, but the no-op itself used to cost a couple of minutes per
# run (apt-get update over the network, a pip resolve, an emsdk git pull and
# re-activate). Each step below now checks first and only reaches for the
# network when something is actually missing. --skip-install still skips the
# lot; these fast paths just mean you rarely need it.
apt_packages_missing() {
	local pkg
	for pkg in "${APT_PACKAGES[@]}"; do
		if ! dpkg-query -W -f='${Status}' "$pkg" 2> /dev/null | grep -q "ok installed"; then
			return 0
		fi
	done
	return 1
}

install_prereqs() {
	step "Installing apt build dependencies..."
	if ! command -v apt-get > /dev/null 2>&1; then
		warn "apt-get not found (not a Debian/Ubuntu system) -- skipping automatic package install."
		warn "Install the equivalent of: ${APT_PACKAGES[*]}"
	elif ! apt_packages_missing; then
		ok "All apt packages already installed -- skipping apt-get."
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
	if command -v scons > /dev/null 2>&1 && scons --version 2> /dev/null | grep -q "v$SCONS_VERSION"; then
		ok "SCons $SCONS_VERSION already installed."
	else
		python3 -m pip install --upgrade pip --quiet --break-system-packages 2> /dev/null \
			|| python3 -m pip install --upgrade pip --quiet
		python3 -m pip install "scons==$SCONS_VERSION" --quiet --break-system-packages 2> /dev/null \
			|| python3 -m pip install "scons==$SCONS_VERSION" --quiet
		command -v scons > /dev/null 2>&1 || die "scons installed via pip but isn't on PATH. Add Python's user-site bin directory to PATH and re-run."
		scons --version
	fi

	if [[ -d "$REPO_ROOT/.git" ]]; then
		step "Updating git submodules..."
		git submodule update --init --recursive
	fi

	step "Checking pre-built AccessKit (downloaded only if the pinned version is missing)..."
	python3 misc/scripts/install_accesskit.py || { warn "AccessKit install failed, building with accesskit=no."; ACCESSKIT_ENABLED="no"; }

	if [[ "$SKIP_WEB" -eq 0 ]]; then
		install_emsdk
	fi
}

install_emsdk() {
	step "Setting up Emscripten SDK ($EMSDK_VERSION) at $EMSDK_DIR..."
	# `emsdk install` re-extracts the toolchain tree (and so throws away this
	# fork's Emscripten patches), and `git pull` + `activate` cost a minute even
	# when they change nothing. If the activated emcc already reports the version
	# this fork pins, there is nothing to do.
	if [[ -f "$EMSDK_DIR/emsdk_env.sh" ]]; then
		local active_version
		active_version="$(
			# shellcheck source=/dev/null
			source "$EMSDK_DIR/emsdk_env.sh" > /dev/null 2>&1
			emcc --version 2> /dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1
		)" || active_version=""
		if [[ "$active_version" == "$EMSDK_VERSION" ]]; then
			ok "Emscripten $EMSDK_VERSION already installed and activated."
			return
		fi
	fi
	if [[ ! -d "$EMSDK_DIR" ]]; then
		git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
	else
		(cd "$EMSDK_DIR" && git pull --ff-only) || warn "Could not update existing emsdk checkout at $EMSDK_DIR, using it as-is."
	fi
	(cd "$EMSDK_DIR" && ./emsdk install "$EMSDK_VERSION" && ./emsdk activate "$EMSDK_VERSION") \
		|| { warn "emsdk install/activate failed -- skipping the web/WebGPU template build."; SKIP_WEB=1; }
}

ACCESSKIT_ENABLED="yes"

if [[ "$PLUGINS_ONLY" -eq 1 ]]; then
	: # The plugins need none of the engine's prerequisites.
elif [[ "$SKIP_INSTALL" -eq 0 ]]; then
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

	local templates_out_dir="$TPZ_COPY_DIR"
	if [[ -d "$templates_out_dir" ]]; then
		cp "$tpz" "$templates_out_dir/"
		ok "Export template package -> $templates_out_dir/$(basename "$tpz")"
	else
		warn "$templates_out_dir not found -- skipping .tpz copy there."
	fi
}

# --- Editor plugins for braced GDScript ---------------------------------------
#
# The Rider plugin and the VS Code extension live in their own repos next to
# this one. They don't link against the engine -- they run the editor built
# below (--gdscript-format-server) to format -- so they need not match this
# build exactly, but building them here keeps everything a .gdb user installs in
# one place. They are built at the end of a run, or alone with --plugins-only.
# Both are cheap when nothing changed (Gradle is incremental; vsce just zips the
# extension), and neither failing is worth failing the run over: by then the
# editor and templates are already built.

# The major version of the JDK whose home is $1 (8 for "1.8.0_432"), or nothing.
jdk_major() {
	local version
	[[ -x "$1/bin/java" ]] || return 1
	version="$("$1/bin/java" -version 2>&1 | head -1 | grep -oE '"[0-9][^"]*"' | tr -d '"')"
	[[ -n "$version" ]] || return 1
	[[ "$version" == 1.* ]] && version="${version#1.}"
	echo "${version%%[^0-9]*}"
}

# Gradle needs JDK 17 or later to run, and the plugin compiles for 21
# (jvmToolchain(21)). JAVA_HOME is often pinned to an older JDK for other tools
# (a shell profile exporting JDK 8, say), and Gradle runs on whatever it names, so
# don't trust it: take the first JDK 21 among JAVA_HOME, the java on PATH, the
# JDKs under /usr/lib/jvm and the one Rider ships (riderPath/jbr), else the first
# JDK 17 or later among them. Prints its home, or fails.
find_gradle_jdk() {
	local candidates=() home major rider_path fallback=""
	[[ -n "${JAVA_HOME:-}" ]] && candidates+=("$JAVA_HOME")
	if command -v java > /dev/null 2>&1; then
		candidates+=("$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")")
	fi
	for home in /usr/lib/jvm/*; do
		[[ -L "$home" ]] || candidates+=("$home")
	done
	rider_path="$(sed -n 's/^[[:space:]]*riderPath[[:space:]]*=[[:space:]]*//p' "$GDB_RIDER_DIR/gradle.properties" 2> /dev/null | head -1)"
	[[ -n "$rider_path" ]] && candidates+=("$rider_path/jbr")
	for home in "${candidates[@]}"; do
		major="$(jdk_major "$home")" || continue
		if [[ "$major" -eq 21 ]]; then
			echo "$home"
			return 0
		fi
		[[ -z "$fallback" && "$major" -ge 17 ]] && fallback="$home"
	done
	[[ -n "$fallback" ]] || return 1
	echo "$fallback"
}

build_rider_plugin() {
	step "Building the Rider plugin for braced GDScript ($GDB_RIDER_DIR)..."
	if [[ ! -x "$GDB_RIDER_DIR/gradlew" ]]; then
		warn "No Rider plugin repo at $GDB_RIDER_DIR (set GDB_RIDER_DIR) -- skipping it."
		return
	fi
	local gradle_jdk
	if ! gradle_jdk="$(find_gradle_jdk)"; then
		warn "No JDK 17 or later found (the Rider plugin needs JDK 21: apt install openjdk-21-jdk) -- skipping it."
		return
	fi
	ok "Gradle runs on $gradle_jdk (JDK $(jdk_major "$gradle_jdk"))."
	# Builds against the local Rider install named by riderPath in the plugin's
	# gradle.properties; a missing Rider fails here, with Gradle saying so.
	# JAVA_HOME is set for Gradle only, not for the rest of this script.
	if ! (cd "$GDB_RIDER_DIR" && JAVA_HOME="$gradle_jdk" PATH="$gradle_jdk/bin:$PATH" ./gradlew buildPlugin --quiet); then
		warn "The Rider plugin build failed -- see the Gradle output above. Engine builds are unaffected."
		return
	fi
	local zip
	zip="$(ls -t "$GDB_RIDER_DIR"/build/distributions/godot-gdb-rider-*.zip 2> /dev/null | head -1)"
	[[ -n "$zip" ]] || { warn "The Rider plugin built, but no zip was found in $GDB_RIDER_DIR/build/distributions."; return; }
	cp "$zip" "$PLUGINS_OUT_DIR/"
	ok "Rider plugin -> $PLUGINS_OUT_DIR/$(basename "$zip") (Settings > Plugins > Install Plugin from Disk)"
}

build_vscode_extension() {
	step "Packaging the VS Code extension for braced GDScript ($GDB_VSCODE_DIR)..."
	if [[ ! -f "$GDB_VSCODE_DIR/package.json" ]]; then
		warn "No VS Code extension repo at $GDB_VSCODE_DIR (set GDB_VSCODE_DIR) -- skipping it."
		return
	fi
	if ! command -v npx > /dev/null 2>&1; then
		warn "npx not found (the .vsix is packaged with Node's npx) -- skipping it."
		return
	fi
	local version vsix
	version="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' "$GDB_VSCODE_DIR/package.json")"
	vsix="godot-gdb-$version.vsix"
	# --no-dependencies: the extension has no runtime dependencies, and its
	# devDependencies (the test harness) must not end up in the package.
	if ! (cd "$GDB_VSCODE_DIR" && npx --yes "@vscode/vsce@$VSCE_VERSION" package \
		--no-dependencies --allow-missing-repository --out "$vsix"); then
		warn "Packaging the VS Code extension failed -- see the output above. Engine builds are unaffected."
		return
	fi
	cp "$GDB_VSCODE_DIR/$vsix" "$PLUGINS_OUT_DIR/"
	ok "VS Code extension -> $PLUGINS_OUT_DIR/$vsix (Extensions > ... > Install from VSIX)"
}

PLUGINS_OUT_DIR="$OUT_DIR/editor_plugins"
build_editor_plugins() {
	mkdir -p "$PLUGINS_OUT_DIR"
	build_rider_plugin
	build_vscode_extension
}

# --plugins-only: nothing else to do. No prerequisites were installed (the
# plugins bring their own toolchains: Gradle and npx), and no SCons run starts,
# so this is safe while another build is running in this tree.
if [[ "$PLUGINS_ONLY" -eq 1 ]]; then
	build_editor_plugins
	print_timings
	echo
	ok "Done. Plugins in builds/$VERSION/editor_plugins/"
	exit 0
fi

# --- Build accelerators + matrix validation ---------------------------------

# Validate the matrix filters before anything expensive runs, so a typo'd
# --web-threads doesn't surface as "built 0 templates" an hour later.
for tok in ${WEB_TARGETS//,/ }; do
	[[ "$tok" == "debug" || "$tok" == "release" ]] || die "--web-targets: expected debug and/or release, got '$tok'."
done
for tok in ${WEB_THREADS//,/ }; do
	[[ "$tok" == "yes" || "$tok" == "no" ]] || die "--web-threads: expected yes and/or no, got '$tok'."
done
for tok in ${WEB_DLINK//,/ }; do
	[[ "$tok" == "yes" || "$tok" == "no" ]] || die "--web-dlink: expected yes and/or no, got '$tok'."
done

# ccache. The 11 SCons invocations below each compile their own ~3,000 objects
# (every variant has its own OBJSUFFIX, so nothing is shared between them by
# design), which is why re-running this script costs nearly as much as the first
# run. ccache makes the second compile of any given translation unit -- after a
# branch switch, a reverted experiment, or just re-running the script because
# one template failed to link -- a cache hit.
#
# Wired in by *masquerade* (a directory of symlinks named after the compilers,
# put at the front of PATH) rather than by SCons' own
# c_compiler_launcher/cpp_compiler_launcher options, so the compile command line
# stays plain `g++`/`em++` and scons' own emscripten detection keeps working.
# Enabling it invalidates every object once: SCons records the resolved compiler
# executable (here the symlink) as an implicit dependency of each object, so the
# first run after ccache became the default recompiles all 11 variants. That is
# a one-time cost, which is why --no-ccache keeps the masquerade on PATH and
# only sets CCACHE_DISABLE=1 -- bypassing the cache without changing the path
# SCons signed the objects with, so toggling it never costs a recompile again.
#
# `emcc` is deliberately NOT symlinked, even though it would cover the ~520 C
# objects per variant: platform/web/detect.py locates the Emscripten root with
# WhereIs("emcc") + realpath() (for the sysroot include path and the JS libs),
# and a ccache symlink there resolves it to /usr/bin, breaking the web build.
# C++ (~2,400 objects/variant) and linking both go through em++, which is safe
# to shadow -- ccache passes a link command (no -c) straight through.
CCACHE_ACTIVE=0
CCACHE_MASQ_DIR=""
setup_ccache() {
	local ccache_bin
	ccache_bin="$(command -v ccache)" || return 1
	CCACHE_MASQ_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/godotwebgpu-ccache-bin"
	mkdir -p "$CCACHE_MASQ_DIR" || return 1
	local name
	for name in cc c++ gcc g++ clang clang++ em++; do
		ln -sf "$ccache_bin" "$CCACHE_MASQ_DIR/$name" || return 1
	done
	# Exported rather than written into the user's ccache.conf: 11 variants of a
	# ~3,000-object build is roughly 2.5 GiB of objects per generation, and the
	# 5 GiB default evicts most of one run before the next one starts.
	export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-50G}"
	CCACHE_ACTIVE=1
	return 0
}

# Has to be re-applied after sourcing emsdk_env.sh, which puts the Emscripten
# toolchain dir at the front of PATH and would otherwise win for em++.
use_ccache_path() {
	[[ "$CCACHE_ACTIVE" -eq 1 ]] || return 0
	export PATH="$CCACHE_MASQ_DIR:$PATH"
}

if ! command -v ccache > /dev/null 2>&1; then
	if [[ "$USE_CCACHE" == "yes" ]]; then
		warn "ccache is not installed (apt install ccache, or run without --skip-install) -- building without it."
	fi
elif setup_ccache; then
	# The masquerade goes on PATH whether or not the cache is in use, so the
	# compiler path SCons signs every object with does not depend on this flag.
	use_ccache_path
	if [[ "$USE_CCACHE" == "yes" ]]; then
		ok "ccache: $(ccache --version | head -1) (max ${CCACHE_MAXSIZE}, via $CCACHE_MASQ_DIR)"
	else
		export CCACHE_DISABLE=1
		ok "ccache: bypassed for this run (--no-ccache); compiler path unchanged, so no recompile. Use 'ccache -C' to clear the cache."
	fi
else
	warn "Could not set up the ccache symlink directory -- building without it."
fi

# Linker for the three native builds. Nothing here changes what is produced;
# mold/lld just link the editor's ~3,400 objects in a fraction of ld.bfd's time
# (measured: ~6s -> ~2s with debug_symbols=no; the gap is a minute or more for a
# dev_build editor). Web builds are unaffected (wasm-ld).
NATIVE_LINKER_ARGS=()
if [[ "$LINKER" == "auto" ]]; then
	if command -v mold > /dev/null 2>&1; then
		LINKER="mold"
	elif command -v ld.lld > /dev/null 2>&1; then
		LINKER="lld"
	else
		LINKER="default"
	fi
fi
if [[ "$LINKER" != "default" ]]; then
	NATIVE_LINKER_ARGS=("linker=$LINKER")
	ok "Native linker: $LINKER"
else
	warn "No mold/lld found -- native links use the system default linker."
fi

if [[ "$WEB_TARGETS,$WEB_THREADS,$WEB_DLINK" != "debug,release,yes,no,yes,no" ]]; then
	warn "Web matrix narrowed (targets=$WEB_TARGETS threads=$WEB_THREADS dlink=$WEB_DLINK)."
	warn "Fine for iteration, but the templates under builds/ are then a mix of"
	warn "this run's output and whatever an earlier run left there -- only a full"
	warn "run guarantees every template's baked WGSL matches this editor's baker."
fi

step "Godot WebGPU Linux build -- version $VERSION, -j$JOBS (accesskit=$ACCESSKIT_ENABLED, gdscript_braces=$GDSCRIPT_BRACES)"

# --- Build -------------------------------------------------------------------

step "Building editor (platform=linuxbsd)..."
# webgpu=yes is required here even though this is the native editor, not a
# web build: it's what makes platform/linuxbsd/detect.py define
# WEBGPU_SHADER_BAKER_ENABLED (gated on `env["webgpu"] and env.editor_build`
# -- see that file), which is what makes the editor's export-time shader
# baker (RenderingShaderContainerWebGPU::_set_code_from_spirv(), see
# webgpu_notes/TASKS.md §4.10) actually bake WGSL at all. Omitting it
# silently produces a working editor that exports fine but never bakes any
# shader, regardless of dev_build.
#
# gdscript_braces is passed to every build below, templates included, even
# though it is the engine's default: the editor and the templates it exports
# with have to agree, because an export from a braced editor ships .gdb scripts
# (as .gdbc) that a template built without it cannot load.
scons platform=linuxbsd target=editor accesskit="$ACCESSKIT_ENABLED" webgpu=yes gdscript_braces="$GDSCRIPT_BRACES" \
	"${NATIVE_LINKER_ARGS[@]}" -j"$JOBS"

# The binary this invocation produces, by Godot's own naming
# (godot.<platform>.<target>.<arch>). Copying `bin/godot.linuxbsd.editor*`
# instead -- as this used to -- also picks up unrelated binaries left in bin/ by
# other builds (a dev_build editor is ~1.1 GB, a double-precision one ~166 MB),
# so every run shipped them to builds/ and editors/ and spent minutes copying
# binaries it had not built.
case "$(uname -m)" in
	x86_64 | amd64) EDITOR_ARCH="x86_64" ;;
	aarch64 | arm64) EDITOR_ARCH="arm64" ;;
	armv7l) EDITOR_ARCH="arm32" ;;
	i*86) EDITOR_ARCH="x86_32" ;;
	*) EDITOR_ARCH="$(uname -m)" ;;
esac
EDITOR_BIN="bin/godot.linuxbsd.editor.$EDITOR_ARCH"
if [[ ! -f "$EDITOR_BIN" ]]; then
	# Fall back to the newest plain editor binary (no .dev/.double/.llvm
	# suffix) rather than guessing wrong about the arch name.
	EDITOR_BIN="$(find bin -maxdepth 1 -name 'godot.linuxbsd.editor.*' \
		! -name '*.dev.*' ! -name '*.double.*' ! -name '*.llvm.*' -type f \
		-printf '%T@ %p\n' 2> /dev/null | sort -rn | head -1 | cut -d' ' -f2-)"
fi
[[ -n "$EDITOR_BIN" && -f "$EDITOR_BIN" ]] || die "Editor build succeeded but no output binary was found in bin/."

# Replacing a binary that is currently being executed fails with ETXTBSY, and a
# headless editor left running from another session is enough to abort the whole
# run right after the editor build -- before any template is built. Write beside
# the destination and rename into place instead: rename() swaps the directory
# entry atomically, and a process already running the old binary keeps its own
# inode and is unaffected.
install_binary() {
	local src="$1" dir="$2" tmp
	tmp="$(mktemp "$dir/.install.XXXXXX")" || return 1
	cp --reflink=auto "$src" "$tmp" || { rm -f "$tmp"; return 1; }
	chmod --reference="$src" "$tmp" 2> /dev/null || chmod 755 "$tmp"
	mv -f "$tmp" "$dir/$(basename "$src")"
}

dest="$OUT_DIR/editor"
mkdir -p "$dest"
install_binary "$EDITOR_BIN" "$dest" || die "Could not install the editor into $dest."
ok "Editor -> $dest/$(basename "$EDITOR_BIN")"

if [[ -d "$EDITORS_DIR" ]]; then
	install_binary "$EDITOR_BIN" "$EDITORS_DIR" || die "Could not install the editor into $EDITORS_DIR."
	ok "Editor -> $EDITORS_DIR"
else
	warn "$EDITORS_DIR not found -- skipping editor copy there."
fi

# The editor's export templates folder for this version, which is where Godot
# looks for templates when exporting. builds/<version>/templates is installed to
# below; it only reaches the editor if it IS that folder (a symlink to it), or
# once the .tpz at the end of the run is installed through the Template Manager.
EDITOR_TEMPLATES_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/godot/export_templates/$VERSION"

if [[ "$SKIP_WGSL_CLI" -eq 0 ]]; then
	step "Building wgsl_convert_cli (native SPIR-V -> WGSL precompile tool)..."
	./drivers/webgpu/wgsl_cli/build.sh
	ok "wgsl_convert_cli -> bin/wgsl_convert_cli"

	# The editor's shader baker (wgsl_bake_subprocess.cpp's
	# _find_wgsl_convert_cli()) looks for wgsl_convert_cli in the same
	# directory as the RUNNING editor executable's own path -- not bin/, and
	# not $PATH. It's silently a no-op (shaders just export unbaked) if it's
	# missing there, so it must be copied alongside every place the editor
	# binary itself gets copied, not just left in bin/.
	cp bin/wgsl_convert_cli "$OUT_DIR/editor/"
	ok "wgsl_convert_cli -> $OUT_DIR/editor"
	if [[ -d "$EDITORS_DIR" ]]; then
		cp bin/wgsl_convert_cli "$EDITORS_DIR/"
		ok "wgsl_convert_cli -> $EDITORS_DIR"
	fi
else
	warn "Skipping wgsl_convert_cli build (--skip-wgsl-cli) -- any web build below will embed"
	warn "whatever WGSL bin/wgsl_convert_cli last produced, which may be stale."
fi

# The native template builds run with the PATH this script had before
# emsdk_env.sh (sourced below for the web builds) rearranged it, so they resolve
# exactly the tools they did when they ran before any web build.
NATIVE_PATH="$PATH"

if [[ "$SKIP_WEB" -eq 0 ]]; then
	# shellcheck source=/dev/null
	source "$EMSDK_DIR/emsdk_env.sh" > /dev/null
	use_ccache_path # emsdk_env.sh just put its own em++ ahead of ccache's.

	# The threads=yes dlink_enabled=yes variants below do not boot without this
	# fork's Emscripten patches, and `emsdk install` above overwrites the toolchain
	# tree -- so they have to be (re-)applied here, after the install, on every run.
	# Idempotent; refuses rather than fuzzily applying if upstream has moved any of
	# the text it pins. A failure only costs the two threads+dlink templates, so
	# warn and keep going rather than abandoning the whole matrix.
	step "Applying Emscripten toolchain patches (needed for threads+GDExtension)..."
	if ./misc/emsdk_patches/apply.sh --apply; then
		ok "Emscripten toolchain patched."
	else
		warn "Could not patch the Emscripten toolchain -- the threads=yes dlink_enabled=yes"
		warn "templates will build but will not boot. See misc/emsdk_patches/README.md."
	fi

	# wgsl_precompiled.gen.h (drivers/webgpu/SCsub) is generated by shelling out
	# to bin/wgsl_convert_cli, and SCons cannot see most of what decides its
	# contents: the binary itself (built inside the action, not by SCons) and the
	# vendored thirdparty/spirv-cross-wgsl + spirv-tools sources behind it. A change
	# to any of those (e.g. a SPIRV-Cross patch) silently NOT
	# regenerating the table would bake stale WGSL into every built-in engine
	# shader class. This script used to force that by deleting the header on
	# every run; drivers/webgpu/SCsub now fingerprints all of it (size+mtime of
	# every vendored/CLI source, the binary's hash, glslangValidator's version)
	# and re-bakes on its own when any of it moves -- so the guarantee holds for
	# a plain `scons` build too, instead of only through this script, and a run
	# that changed nothing in the shader pipeline no longer re-bakes 275 modules.
	if [[ "$FORCE_SHADER_REBAKE" -eq 1 ]]; then
		step "Removing drivers/webgpu/wgsl_precompiled.gen.h (--force-shader-rebake)..."
		rm -f drivers/webgpu/wgsl_precompiled.gen.h
	fi

	WEB_DEST="$OUT_DIR/templates"
	mkdir -p "$WEB_DEST"
fi

# Builds one web/WebGPU template and installs it into builds/<version>/templates.
# Filenames on both sides (scons output and the templates/ dir) must match
# Godot's own naming rules exactly:
#   - scons suffix order: .wasm32[.nothreads][.dlink]
#     (SConstruct's ".nothreads" is appended before detect.py's ".dlink")
#   - export_plugin.h's _get_template_name(): "web" + ["_dlink"] +
#     ["_nothreads"] + "_debug.zip"/"_release.zip"
# Leaves the installed file's name in WEB_LAST_INSTALLED.
build_web_template() {
	local dlink="$1" threads="$2" target="$3"
	local debug_label="debug"
	[[ "$target" == "template_release" ]] && debug_label="release"

	step "Building web/WebGPU export template ($debug_label, dlink=$dlink, threads=$threads)..."
	scons platform=web target="$target" dlink_enabled="$dlink" webgpu=yes opengl3=no \
		threads="$threads" gdscript_braces="$GDSCRIPT_BRACES" module_raycast_enabled="$WEB_RAYCAST" -j"$JOBS"

	local scons_zip="bin/godot.web.${target}.wasm32"
	[[ "$threads" == "no" ]] && scons_zip+=".nothreads"
	[[ "$dlink" == "yes" ]] && scons_zip+=".dlink"
	scons_zip+=".zip"
	[[ -f "$scons_zip" ]] || die "Web template build succeeded but $scons_zip wasn't found."

	local dest_name="web"
	[[ "$dlink" == "yes" ]] && dest_name+="_dlink"
	[[ "$threads" == "no" ]] && dest_name+="_nothreads"
	dest_name+="_${debug_label}.zip"

	cp "$scons_zip" "$WEB_DEST/$dest_name"
	ok "Web template -> $WEB_DEST/$dest_name"
	WEB_LAST_INSTALLED="$dest_name"
}

# The no-threads, no-dlink pair first (TASKS.md 2.50): it is what most exports
# use, so it is built and installed right after the editor and the converter,
# and the user can export while the rest of the run builds. The editor and these
# templates come from the same tree in the same run, so their hashes match.
if [[ "$SKIP_WEB" -eq 0 ]] && list_has no "$WEB_DLINK" && list_has no "$WEB_THREADS"; then
	ready_files=()
	for target in template_debug template_release; do
		debug_label="debug"
		[[ "$target" == "template_release" ]] && debug_label="release"
		list_has "$debug_label" "$WEB_TARGETS" || continue
		build_web_template no no "$target"
		ready_files+=("$WEB_LAST_INSTALLED")
	done
	_close_step
	READY_AT="$SECONDS"
	echo
	ok "${BOLD}==> Ready to export ($(fmt_duration "$READY_AT") into the run): ${ready_files[*]} installed in $WEB_DEST${NC}"
	if [[ "$(readlink -f "$WEB_DEST")" != "$(readlink -f "$EDITOR_TEMPLATES_DIR" 2> /dev/null || echo "$EDITOR_TEMPLATES_DIR")" ]]; then
		warn "    $WEB_DEST is not the editor's export templates folder ($EDITOR_TEMPLATES_DIR):"
		warn "    copy them there, or symlink that folder to $WEB_DEST, to export with them now."
	fi
	ok "    The rest of the templates build next; exporting with these two is safe meanwhile."
elif [[ "$SKIP_WEB" -eq 0 ]]; then
	warn "The web matrix filters exclude threads=no dlink_enabled=no -- no early export-ready pair this run."
fi

if [[ "$SKIP_NATIVE_TEMPLATES" -eq 0 ]]; then
	step "Building Linux export template (debug)..."
	env PATH="$NATIVE_PATH" scons platform=linuxbsd target=template_debug accesskit="$ACCESSKIT_ENABLED" \
		gdscript_braces="$GDSCRIPT_BRACES" "${NATIVE_LINKER_ARGS[@]}" -j"$JOBS"

	step "Building Linux export template (release)..."
	env PATH="$NATIVE_PATH" scons platform=linuxbsd target=template_release accesskit="$ACCESSKIT_ENABLED" \
		gdscript_braces="$GDSCRIPT_BRACES" "${NATIVE_LINKER_ARGS[@]}" -j"$JOBS"

	dest="$OUT_DIR/templates_linux"
	mkdir -p "$dest"
	dbg="bin/godot.linuxbsd.template_debug.$EDITOR_ARCH"
	rel="bin/godot.linuxbsd.template_release.$EDITOR_ARCH"
	[[ -f "$dbg" && -f "$rel" ]] || die "Linux template build succeeded but a binary wasn't found in bin/."
	# Renamed to match EditorExportPlatformLinuxBSD::get_template_file_name()
	# ("linux_" + target + "." + arch) so these drop straight into
	# ~/.local/share/godot/export_templates/$VERSION/.
	cp --reflink=auto "$dbg" "$dest/linux_debug.$EDITOR_ARCH"
	cp --reflink=auto "$rel" "$dest/linux_release.$EDITOR_ARCH"
	ok "Linux templates -> $dest"
else
	warn "Skipping the Linux export templates (--skip-native-templates)."
fi

if [[ "$SKIP_WEB" -eq 0 ]]; then
	# The rest of the web template matrix: GDExtension/dlink support x thread
	# support x debug/release (8 builds with the pair above), so every
	# combination the Web export preset's "GDExtension Support" / "Thread Support"
	# checkboxes can select has a matching template.
	for dlink in yes no; do
		list_has "$dlink" "$WEB_DLINK" || continue
		for threads in yes no; do
			list_has "$threads" "$WEB_THREADS" || continue
			[[ "$dlink" == "no" && "$threads" == "no" ]] && continue # Built first, above.
			for target in template_debug template_release; do
				debug_label="debug"
				[[ "$target" == "template_release" ]] && debug_label="release"
				list_has "$debug_label" "$WEB_TARGETS" || continue
				build_web_template "$dlink" "$threads" "$target"
			done
		done
	done
else
	warn "Skipping web/WebGPU template build (--skip-web)."
fi

if [[ "$GDSCRIPT_BRACES" == "no" ]]; then
	warn "Skipping the braced GDScript editor plugins (--no-gdscript-braces): they format through a braced editor."
elif [[ "$SKIP_EDITOR_PLUGINS" -eq 1 ]]; then
	warn "Skipping the braced GDScript editor plugins (--skip-editor-plugins)."
else
	build_editor_plugins
fi

package_tpz

print_timings

# How much of this run came out of the cache -- the number to look at before
# wondering why a re-run still took an hour.
if [[ "$CCACHE_ACTIVE" -eq 1 ]]; then
	ccache_stats="$(ccache --show-stats 2> /dev/null | grep -iE 'hit|miss|size' | head -4 | sed 's/^/  /')"
	if [[ -n "$ccache_stats" ]]; then
		echo -e "\n${BOLD}==> ccache${NC}"
		echo "$ccache_stats"
	fi
fi

echo
ok "Done. Output in builds/$VERSION/"
