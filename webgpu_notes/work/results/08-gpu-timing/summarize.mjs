// node summarize.mjs <compare.sh out dir> [--passes-top 25] [--min-ms 0.02]
// Tables (markdown) from compare.sh's logs: per scene and config, fps, GPU busy ms/frame (per command buffer runs),
// and per pass GPU ms/frame (--passes runs), each as mean ± half the min..max spread over the runs. A pass row is
// keyed by its pipelines and first attachment, the label gpu_time.mjs prints.
import { readdirSync, readFileSync } from 'fs';
import { join } from 'path';
const dir = process.argv[2];
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const TOP = +arg('passes-top', 25), MIN = +arg('min-ms', 0.02);
const data = {}; // scene -> tag -> { fps: [], busy: [], pbusy: [], passes: {key: []}, src: '' , adapter: '' }
for (const f of readdirSync(dir)) {
	const m = f.match(/^(.+)\.([^.]+)\.(\d+)\.(cb|passes)\.log$/);
	if (!m) continue;
	const [, scene, tag, run, kind] = m;
	const d = ((data[scene] ||= {})[tag] ||= { fps: [], busy: [], pbusy: [], passes: {}, src: new Set(), adapter: new Set(), runs: new Set() });
	const t = readFileSync(join(dir, f), 'utf8');
	const fps = t.match(/-> (\d+) fps/), busy = t.match(/GPU busy ([\d.]+) ms\/frame/);
	const src = t.match(/shader sources: (.*)/), ad = t.match(/\[gpu_time\] adapter (.*)/);
	if (src) d.src.add(src[1]);
	if (ad) d.adapter.add(ad[1]);
	if (t.includes('CONTAMINATED')) { console.error(`dropped (contaminated): ${f}`); continue; }
	if (!fps || !busy) { console.error(`no result in ${f}`); continue; }
	if (kind === 'cb') { d.fps.push(+fps[1]); d.busy.push(+busy[1]); d.runs.add(run); continue; }
	d.pbusy.push(+busy[1]);
	const seen = {};
	for (const line of t.split('\n')) {
		const r = line.match(/^\s*([\d.]+)\s+([\d.]+)\s+(\d+)\s+(render |compute|copy   ) (.*)$/);
		if (r) seen[`${r[4].trim()} ${r[5].replace(/\s+/g, ' ').trim()}`] = +r[1];
	}
	// A pass missing from one run counts as 0 there, so every key has one value per run.
	const idx = d.pbusy.length - 1;
	for (const [k, v] of Object.entries(seen)) { const a = (d.passes[k] ||= []); while (a.length < idx) a.push(0); a.push(v); }
	for (const a of Object.values(d.passes)) while (a.length < idx + 1) a.push(0);
}
const mean = (a) => a.reduce((s, x) => s + x, 0) / a.length;
const med = (a) => { const s = [...a].sort((x, y) => x - y); return s.length % 2 ? s[s.length >> 1] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2; };
const ms = (a, p = 2) => (a.length ? `${mean(a).toFixed(p)} ± ${((Math.max(...a) - Math.min(...a)) / 2).toFixed(p)}` : '-');
for (const [scene, tags] of Object.entries(data)) {
	const names = Object.keys(tags).sort();
	console.log(`\n### ${scene}\n`);
	console.log(`| config | runs | fps | GPU ms/frame (cb) | median GPU ms/frame (cb) | GPU ms/frame (sum of passes) | median (passes) | shader sources |`);
	console.log(`|---|---|---|---|---|---|---|---|`);
	for (const n of names) { const d = tags[n]; console.log(`| ${n} | ${d.fps.length} | ${ms(d.fps, 0)} | ${ms(d.busy, 3)} | ${d.busy.length ? med(d.busy).toFixed(3) : '-'} | ${ms(d.pbusy, 3)} | ${d.pbusy.length ? med(d.pbusy).toFixed(3) : '-'} | ${[...d.src].join(' / ')} |`); }
	const keys = new Set(names.flatMap((n) => Object.keys(tags[n].passes)));
	const rows = [...keys].map((k) => ({ k, v: names.map((n) => tags[n].passes[k] || []) })).filter((r) => Math.max(...r.v.map((a) => (a.length ? mean(a) : 0))) >= MIN);
	rows.sort((a, b) => Math.max(...b.v.map((x) => (x.length ? mean(x) : 0))) - Math.max(...a.v.map((x) => (x.length ? mean(x) : 0))));
	if (!rows.length) continue;
	console.log(`\n| pass (pipelines -> attachment) | ${names.join(' | ')} | ${names.length === 2 ? 'Δ ' + names[1] + '−' + names[0] + ' |' : ''}`);
	console.log(`|---|${names.map(() => '---|').join('')}${names.length === 2 ? '---|' : ''}`);
	for (const r of rows.slice(0, TOP)) {
		const delta = names.length === 2 && r.v[0].length && r.v[1].length ? ` ${(mean(r.v[1]) - mean(r.v[0])).toFixed(3)} |` : names.length === 2 ? ' - |' : '';
		console.log(`| \`${r.k.slice(0, 110)}\` | ${r.v.map((a) => ms(a, 3)).join(' | ')} |${delta}`);
	}
}
const adapters = new Set(Object.values(data).flatMap((t) => Object.values(t).flatMap((d) => [...d.adapter])));
console.log(`\nadapter(s): ${[...adapters].join(' / ') || 'not logged'}`);
