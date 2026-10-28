#!/usr/bin/env bash
D=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w34
cd $D; mkdir -p web
cap() { name=$1; echo "== web $name: $2"; timeout 200 node web_capture.mjs $D/webexp web/$name.png "$2" 1280x720; }
cap blur_s20 "test=blur&blur=1&attrs=physical&shutter=20&speed=10&frames=60"
cap blur_s20_msaa "test=blur&blur=1&attrs=physical&shutter=20&speed=10&frames=60&msaa=2"
cap rolling "test=rolling&rolling=1&readout=30&omega=2&frames=60"
cap sky "test=sky&rolling=1&readout=30&omega=2&frames=60"
cap sky_msaa "test=sky&rolling=1&readout=30&omega=2&frames=60&msaa=2"
cap look "test=look&blur=1&angle=360&rolling=1&readout=30&speed=20&frames=36"
echo WEB DONE
