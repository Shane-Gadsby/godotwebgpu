#!/bin/bash
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w64
nvidia-smi --query-gpu=memory.used --format=csv
for p in proj projplu; do
  $S/run_native.sh $S/pre/godot.linuxbsd.editor.x86_64 pre_$p $S/$p legacy
  $S/run_native.sh $S/post64/godot.linuxbsd.editor.x86_64 post64_$p $S/$p legacy
done
$S/run_native.sh $S/post64/godot.linuxbsd.editor.x86_64 post64_n64 $S/proj neutral64
# Task 65: opt-in off must equal the pre-change editor (legacy set), and the opt-in checks.
for p in proj projplu; do
  $S/run_native.sh $S/post65/godot.linuxbsd.editor.x86_64 post65_$p $S/$p legacy
done
$S/run_native.sh $S/post65/godot.linuxbsd.editor.x86_64 post65_opt $S/proj all
$S/run_native.sh $S/post65/godot.linuxbsd.editor.x86_64 post65_opt_plu $S/projplu neutral65
# Web (WebGPU Forward+): export from the post-65 editor with this tree's template_debug, then Chrome.
until grep -q "build exit 0" $S/build_web.log 2>/dev/null; do sleep 15; done
mkdir -p $S/webexport && rm -rf $S/webexport/*
xvfb-run -a -s "-screen 0 1280x720x24" $S/post65/godot.linuxbsd.editor.x86_64 --path $S/webproj --rendering-driver vulkan --export-debug Web $S/webexport/index.html > $S/webexport.log 2>&1; echo "export rc $?"
ls $S/webexport
node $S/run_web.mjs $S/webexport $S/out/web > $S/web.log 2>&1; echo "web rc $?"
