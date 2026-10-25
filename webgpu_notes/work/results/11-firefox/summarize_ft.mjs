// summarize_ft.mjs <out dir>: tabulate compare_ft.sh logs per scene and config. From <scene>.<tag>.<run>.ft.log
// (frame_time.mjs, uninstrumented): fps and median ms per frame; from .gt.log (gpu_time.mjs, timestamp queries):
// GPU busy ms per frame. Each cell is the median over launches with the spread (min..max). CONTAMINATED logs are
// dropped and counted.
import { readdirSync, readFileSync } from 'fs';
import { join } from 'path';
const dir = process.argv[2];
const rows = {};
let dropped = 0;
const cell = (scene, tag) => ((rows[scene] ||= {})[tag] ||= { fps: [], median: [], p95: [], gpu: [], shaders: new Set(), renderer: new Set(), errors: 0, onGpu: 0, ftRuns: 0 });
for (const f of readdirSync(dir).filter((x) => x.endsWith('.log')).sort()) {
	const m = f.match(/^(.+)\.([^.]+)\.(\d+)\.(ft|gt)\.log$/);
	if (!m) continue;
	const t = readFileSync(join(dir, f), 'utf8');
	if (t.includes('CONTAMINATED')) { dropped++; continue; }
	const r = cell(m[1], m[2]);
	if (m[4] === 'ft') {
		const fm = t.match(/-> ([\d.]+) fps, mean ([\d.]+) ms\/frame, median ([\d.]+), p95 ([\d.]+)/);
		if (!fm) continue;
		r.ftRuns++;
		r.fps.push(+fm[1]); r.median.push(+fm[3]); r.p95.push(+fm[4]);
		r.shaders.add((t.match(/shader sources: (.*)/) || [])[1] || '?');
		r.renderer.add(/Forward\+/.test(t) ? 'Forward+' : /Mobile/.test(t) ? 'Mobile' : '?');
		r.errors += +((t.match(/console errors: (\d+)/) || [])[1] || 0);
		if (/nvidia-smi: .*(firefox|chrome)/i.test(t)) r.onGpu++;
	} else {
		const g = t.match(/GPU busy ([\d.]+) ms\/frame/);
		if (g) r.gpu.push(+g[1]);
	}
}
const med = (a) => { if (!a.length) return NaN; const s = [...a].sort((x, y) => x - y); return s.length % 2 ? s[s.length >> 1] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2; };
const fmt = (a, d = 3) => (a.length ? `${med(a).toFixed(d)} (${Math.min(...a).toFixed(d)}..${Math.max(...a).toFixed(d)})` : 'n/a');
console.log('| scene | config | launches ft/gt | fps (uninstrumented) | median ms/frame | p95 ms/frame | GPU busy ms/frame | renderer | console errors | browser on GPU | shaders |');
console.log('|---|---|---|---|---|---|---|---|---|---|---|');
for (const [scene, cfgs] of Object.entries(rows)) {
	for (const [tag, r] of Object.entries(cfgs)) {
		console.log(`| ${scene} | ${tag} | ${r.ftRuns}/${r.gpu.length} | ${fmt(r.fps, 0)} | ${fmt(r.median)} | ${fmt(r.p95)} | ${fmt(r.gpu, 2)} | ${[...r.renderer].join(',')} | ${r.errors} | ${r.onGpu}/${r.ftRuns} | ${[...r.shaders].join(' ')} |`);
	}
}
if (dropped) console.log(`\n${dropped} contaminated log(s) dropped`);
