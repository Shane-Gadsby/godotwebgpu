#!/usr/bin/env bash
# Export the bench for the web with this worktree's templates (copied to tpl/), args gain --verbose.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w80
W=/mnt/109313D2109313D2/godot-editors/godotwebgpu-80
mkdir -p $S/tpl && cp $W/bin/godot.web.template_*.wasm32.nothreads.zip $S/tpl/
for kind in release debug; do
	rm -rf $S/exp_$kind; mkdir -p $S/exp_$kind
	timeout 600 xvfb-run -a -s "-screen 0 1920x1080x24" $S/new/godot.linuxbsd.editor.x86_64 --path $S/bench --rendering-driver vulkan \
		--export-$kind Web $S/exp_$kind/index.html > $S/exp_$kind.log 2>&1
	sed -i 's/"args":\[\]/"args":["--verbose"]/' $S/exp_$kind/index.html
	grep -q '"args":\["--verbose"\]' $S/exp_$kind/index.html || echo "NO VERBOSE in $kind"
	ls -la $S/exp_$kind | grep -c index
done
