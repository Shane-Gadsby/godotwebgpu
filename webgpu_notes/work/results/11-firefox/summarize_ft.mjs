// summarize_ft.mjs <out dir>: tabulate compare_ft.sh logs per scene and config: fps and mean/median ms per frame as
// median of the launches, with the spread (min..max) over launches. Drops CONTAMINATED logs and says so.
import { readdirSync, readFileSync } from 'fs';
import { join } from 'path';
const dir = process.argv[2];
const rows = {};
let dropped = 0;
for (const f of readdirSync(dir).filter((x) => x.endsWith('.log')).sort()) {
	const m = f.match(/^(.+)\.([^.]+)\.(\d+)\.log$/);
	if (!m) continue;
	const t = readFileSync(join(dir, f), 'utf8');
	if (t.includes('CONTAMINATED')) { dropped++; continue; }
	const fm = t.match(/-> ([\d.]+) fps, mean ([\d.]+) ms\/frame, median ([\d.]+), p95 ([\d.]+)/);
	if (!fm) continue;
	const sh = (t.match(/shader sources: (.*)/) || [])[1] || '';
	const r = (rows[m[1]] ||= {})[m[2]] ||= { fps: [], mean: [], median: [], p95: [], shaders: new Set(), renderer: new Set() };
	r.fps.push(+fm[1]); r.mean.push(+fm[2]); r.median.push(+fm[3]); r.p95.push(+fm[4]); r.shaders.add(sh);
	r.renderer.add(((t.match(/renderer: (.*)/) || [])[1] || '').replace(/^.*?(Forward\S*|Mobile).*$/, '$1'));
}
const med = (a) => { const s = [...a].sort((x, y) => x - y); return s.length % 2 ? s[s.length >> 1] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2; };
const fmt = (a, d = 3) => `${med(a).toFixed(d)} (${Math.min(...a).toFixed(d)}..${Math.max(...a).toFixed(d)})`;
console.log('| scene | config | launches | fps | mean ms/frame | median ms/frame | p95 ms/frame | renderer | shaders |');
console.log('|---|---|---|---|---|---|---|---|---|');
for (const [scene, cfgs] of Object.entries(rows)) {
	for (const [tag, r] of Object.entries(cfgs)) {
		console.log(`| ${scene} | ${tag} | ${r.fps.length} | ${fmt(r.fps, 0)} | ${fmt(r.mean)} | ${fmt(r.median)} | ${fmt(r.p95)} | ${[...r.renderer].join(',')} | ${[...r.shaders].join(' ')} |`);
	}
}
if (dropped) console.log(`\n${dropped} contaminated log(s) dropped`);
