#!/usr/bin/env bash
# export_web.sh: exports the test project with work/57's template_debug to $S/web (game.html) under xvfb with Vulkan,
# so the export-time WGSL bake runs (TASKS.md §3 trap 83), then makes $S/web_on and $S/web_off for gpu_time.mjs (which
# loads "/" with no query): the export's files linked, and an index.html that asserts the adapter (nvidia/lovelace,
# not fallback, 48 sampled textures; logged as a [gpu_time] line) and only then loads game.html with le=1 / le=0.
set -euo pipefail
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w57
R=/mnt/109313D2109313D2/godot-editors/godotwebgpu-57
TEMPLATE=$R/bin/godot.web.template_debug.wasm32.nothreads.zip
OUT=$S/web
rm -rf $OUT; mkdir -p $OUT
cat > $S/proj/export_presets.cfg <<PRESET
[preset.0]

name="Web"
platform="Web"
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path="$OUT/game.html"
script_export_mode=1

[preset.0.options]

custom_template/debug="$TEMPLATE"
custom_template/release=""
variant/extensions_support=false
variant/thread_support=false
html/export_icon=false
html/canvas_resize_policy=2
PRESET
timeout 600 xvfb-run -a -s "-screen 0 1920x1080x24" $R/bin/godot.linuxbsd.editor.x86_64 --rendering-driver vulkan --path $S/proj --import >/dev/null 2>&1 || true
timeout 600 xvfb-run -a -s "-screen 0 1920x1080x24" $R/bin/godot.linuxbsd.editor.x86_64 --rendering-driver vulkan --path $S/proj --export-debug "Web" $OUT/game.html 2>&1 | tail -3
rm -f $S/proj/export_presets.cfg
for v in on off; do
	q="le=1"; [[ $v == off ]] && q="le=0"
	D=$S/web_$v; rm -rf $D; mkdir -p $D
	for f in $OUT/*; do ln -s $f $D/; done
	cat > $D/index.html <<HTML
<!DOCTYPE html><html><head><meta charset="utf-8"><title>redirect</title></head><body><script>
(async () => {
	const a = await navigator.gpu.requestAdapter();
	const info = a ? { vendor: a.info.vendor, arch: a.info.architecture, fallback: a.info.isFallbackAdapter, tex: a.limits.maxSampledTexturesPerShaderStage } : null;
	console.log('[gpu_time] adapter ' + JSON.stringify(info));
	if (!info || info.vendor !== 'nvidia' || info.arch !== 'lovelace' || info.fallback || info.tex < 48) {
		console.log('[gpu_time] ADAPTER BAD: not loading');
		return;
	}
	location.replace('game.html?$q&hdr=0&tonemap=filmic');
})();
</script></body></html>
HTML
done
echo "<!DOCTYPE html><html><body>probe</body></html>" > $OUT/nothing.html
ls -la $OUT | head -20
