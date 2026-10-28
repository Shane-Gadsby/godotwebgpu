#!/bin/bash
# export.sh <editor> <project> <outdir>
ed=$1; p=$2; o=$3; rm -rf $o; mkdir -p $o
timeout 600 xvfb-run -a -s "-screen 0 1600x900x24" $ed --path $p --rendering-driver vulkan --import > $o.import.log 2>&1
timeout 600 xvfb-run -a -s "-screen 0 1600x900x24" $ed --path $p --rendering-driver vulkan --export-debug Web $o/index.html > $o.log 2>&1
echo "export rc=$? $(ls $o | tr '\n' ' ')"
