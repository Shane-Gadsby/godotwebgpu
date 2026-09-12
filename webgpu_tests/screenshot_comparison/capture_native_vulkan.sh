#!/usr/bin/env bash
# Live-repro native Vulkan capture: runs a Godot project directly (not the
# editor UI) through the native Vulkan/Forward+ backend and records a PNG
# frame sequence, for direct visual/behavioral comparison against a WebGPU
# capture of the identical project (see capture_console.mjs /
# capture_screenshots.mjs and LIVE_REPRO_METHODOLOGY.md).
#
# Usage:
#   capture_native_vulkan.sh <godot_binary> <project_dir> <out_dir> [duration_seconds] [fps]
#
# Example (300 simulated seconds at 60fps, matching a 300000ms/20000ms-interval
# WebGPU capture_screenshots.mjs run):
#   ./capture_native_vulkan.sh bin/godot.linuxbsd.editor.dev.x86_64 \
#       /tmp/scratch/my_project /tmp/scratch/vulkan_frames 300 60
#
# NOTE on resolution: the CLI --resolution flag only sets the OS window size,
# NOT the movie capture's internal resolution -- with the common "viewport"
# stretch mode, the capture always comes out at the project's own
# project.godot `window/size/viewport_width`/`viewport_height`. To shrink a
# long capture's disk footprint, edit those two lines directly in the scratch
# copy's project.godot before running this script.
#
# Output: <out_dir>/frame00000000.png ... frameNNNNNNNN.png (one per simulated
# frame) plus a .wav track (ignorable). Sample frame `frame%08d.png` at index
# `seconds*fps - 1` to compare against a WebGPU screenshot taken at the same
# simulated second.
#
# Why not --headless: --headless forces --display-driver headless, which only
# supports the "dummy" (null) rendering driver -- no real Vulkan frames are
# produced at all. This needs a genuine X11/Wayland display with real GPU
# access (check `echo $DISPLAY` and `xdpyinfo`); most sandboxes that have a
# desktop session at all also have real GPU access via it.
#
# Why --fixed-fps + --disable-vsync: decouples simulated time from wall-clock
# speed. The engine still advances one fixed timestep per rendered frame, but
# renders as fast as the GPU allows (not vsync-limited), so N simulated
# seconds can complete in well under N real seconds -- essential for matching
# durations against a real-time WebGPU/Playwright capture without waiting
# through the same duration twice. PNG encoding, not GPU rendering, is
# typically the actual bottleneck (~25ms/frame observed on a 640x360 capture)
# -- shrink the project's project.godot `window/size/viewport_width`/`_height`
# first if a long capture is producing too much disk I/O or taking too long.

set -euo pipefail

GODOT_BIN="${1:?Usage: capture_native_vulkan.sh <godot_binary> <project_dir> <out_dir> [duration_seconds] [fps]}"
PROJECT_DIR="${2:?missing project_dir}"
OUT_DIR="${3:?missing out_dir}"
DURATION_S="${4:-60}"
FPS="${5:-60}"

FRAMES=$((DURATION_S * FPS))

if [[ -z "${DISPLAY:-}" ]]; then
    echo "ERROR: \$DISPLAY is not set. This capture needs a real X11/Wayland display with GPU access -- --headless will NOT work (see script header)." >&2
    exit 1
fi

mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/*.png "$OUT_DIR"/*.wav

# Re-import if this is a fresh scratch copy with no .godot/ cache yet -- a
# missing import step makes --write-movie fail immediately with "Main scene's
# path could not be resolved from UID." Safe/cheap to always run.
"$GODOT_BIN" --path "$PROJECT_DIR" --rendering-driver vulkan --import

echo "Capturing ${DURATION_S}s (${FRAMES} frames @ ${FPS}fps) from $PROJECT_DIR to $OUT_DIR ..."
"$GODOT_BIN" --path "$PROJECT_DIR" --rendering-driver vulkan \
    --write-movie "$OUT_DIR/frame.png" \
    --fixed-fps "$FPS" --disable-vsync --quit-after "$FRAMES"

echo "Done. Frames in $OUT_DIR (frame00000000.png .. frame$(printf '%08d' $((FRAMES - 1))).png)."
