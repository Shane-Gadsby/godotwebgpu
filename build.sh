#!/usr/bin/env bash
# build.sh — Interactive build menu for Godot WebGPU.
#
# Builds the editor and/or the web WebGPU export templates, and copies the
# resulting artifacts into builds/<version>/<editor|templates>/.
#
# Usage: ./build.sh
#
# Env overrides:
#   JOBS=N          parallel build jobs (default: nproc)
#   EMSDK_DIR=path  Emscripten SDK location (default: ~/emsdk)

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

build_editor() {
	echo -e "${BOLD}Building editor (platform=$HOST_PLATFORM)...${NC}"
	scons platform="$HOST_PLATFORM" target=editor -j"$JOBS"

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
}

build_templates() {
	if [[ ! -f "$EMSDK_DIR/emsdk_env.sh" ]]; then
		echo -e "${RED}Emscripten not found at $EMSDK_DIR (set EMSDK_DIR to override).${NC}" >&2
		exit 1
	fi
	# shellcheck source=/dev/null
	source "$EMSDK_DIR/emsdk_env.sh" > /dev/null

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
}

show_menu() {
	echo
	echo -e "${BOLD}Godot WebGPU Build${NC}"
	echo "  Version: $VERSION"
	echo "  Output:  builds/$VERSION/"
	echo
	echo "  1) Editor only"
	echo "  2) Templates only (web, debug + release)"
	echo "  3) Editor + templates"
	echo "  4) Quit"
	echo
}

main() {
	show_menu
	read -rp "Select an option [1-4]: " choice
	case "$choice" in
		1) build_editor ;;
		2) build_templates ;;
		3)
			build_editor
			build_templates
			;;
		4)
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
