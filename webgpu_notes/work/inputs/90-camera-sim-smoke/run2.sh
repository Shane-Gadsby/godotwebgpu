#!/bin/bash
# run.sh <out_dir>  -- A/B steps on the camera-sim main scene
S="$(cd "$(dirname "$0")" && pwd)"  # this folder (copied from camera-sim's scratchpad, 2026-10-30)
cd /mnt/109313D2109313D2/godot-game-dev/camera-sim
SCENE=res://scenes/camera_studio.tscn OUT=$1 EMBED=1 \
STEPS="hide=AcknowledgementsPopup,expr=seed(1),press=PauseAnimation,ShutterSlider=12,ApertureSlider=1.4,shot,capture,FocusDistSlider=2.5,shot,press=AutoFocus,press=MFT,shot,press=APSC,shot,press=FullFrame,LensSlider=200,shot,LensSlider=24,shot,LensSlider=50,ApertureSlider=8,ISOSlider=3200,shot,press=MFT,shot,press=FullFrame,toggle=ExaggerateNoise,shot,dump" \
DISPLAY=:0 timeout 300 /mnt/109313D2109313D2/godot-editors/godotwebgpu/bin/godot.linuxbsd.editor.x86_64 --path . --rendering-driver vulkan --resolution 1656x968 --fixed-fps 60 \
  --script "$S/ab_harness.gd" 2>&1 | grep -v '^\s*$' | grep -iv 'vulkan\|^godot engine\|metahuman.*verbose'
