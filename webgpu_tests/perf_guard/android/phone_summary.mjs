// phone_summary.mjs <phone_bench out dir> [--ref <native reference dir>]
// Tabulates phone_bench.mjs launches: per (scene, browser, config) mean +- half range and median of fps, frame ms
// and GPU ms/frame over the runs, boot time, errors; then, when sweep frames exist, each frame's mean absolute
// difference (0-255, run_forward_plus.mjs's metric) against the native reference and between the configs.
import { readdirSync, readFileSync, existsSync } from 'fs';
import { join } from 'path';
import { decodePng } from '../../screenshot_comparison/png.mjs';
import { meanAbsDiff } from '../../forward_plus/run_forward_plus.mjs';

const dir = process.argv[2];
const ri = process.argv.indexOf('--ref'); const REF = ri !== -1 ? process.argv[ri + 1] : null;
const rows = {};
for (const f of readdirSync(dir).filter((x) => x.endsWith('.json'))) {
	const o = JSON.parse(readFileSync(join(dir, f), 'utf8'));
	const k = `${o.scene}|${o.browser}|${o.tag}`;
	(rows[k] ||= []).push(o);
}
const stat = (xs) => {
	xs = xs.filter((x) => x !== null && x !== undefined && !Number.isNaN(x));
	if (!xs.length) return '-';
	const s = xs.slice().sort((a, b) => a - b), mean = xs.reduce((a, b) => a + b, 0) / xs.length;
	const med = s.length % 2 ? s[s.length >> 1] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2;
	const d = mean >= 100 ? 0 : mean >= 10 ? 1 : 2;
	return `${mean.toFixed(d)} ± ${((s[s.length - 1] - s[0]) / 2).toFixed(d)} (med ${med.toFixed(d)})`;
};
console.log('| scene | browser | config | ok/runs | fps | frame ms | GPU ms/frame | boot s | errors/run | shaders baked/translated | renderer |');
console.log('|---|---|---|---|---|---|---|---|---|---|---|');
for (const k of Object.keys(rows).sort()) {
	const [scene, browser, tag] = k.split('|'); const L = rows[k]; const ok = L.filter((o) => o.result);
	const r = (f) => ok.map((o) => f(o.result));
	const sh = ok[0] && ok[0].result.shaders; const ren = ok[0] && ok[0].result.banner ? ok[0].result.banner.split(' - ')[1] : (L[0].fatal ? 'FAILED: ' + L[0].fatal.text.slice(0, 60) : '?');
	console.log(`| ${scene} | ${browser} | ${tag} | ${ok.length}/${L.length} | ${stat(r((x) => x.fps))} | ${stat(r((x) => x.meanMs))} | ${stat(r((x) => x.gpuMsPerFrame))} | ${stat(ok.map((o) => o.boot.bootMs / 1000))} | ${stat(r((x) => x.errors.length))} | ${sh ? sh.baked + '/' + sh.translated : '-'} | ${ren} |`);
}
const fr = join(dir, 'frames');
if (existsSync(fr)) {
	const sets = readdirSync(fr);
	const load = (p) => (existsSync(p) ? decodePng(readFileSync(p)) : null);
	const names = [...new Set(sets.flatMap((s) => readdirSync(join(fr, s)).filter((x) => x.endsWith('.png'))))].sort();
	console.log(`\n| frame | ${sets.map((s) => s + (REF ? ' vs native' : '')).join(' | ')} | ${sets.length === 2 ? sets.join(' vs ') : ''} |`);
	console.log(`|---|${sets.map(() => '---').join('|')}|---|`);
	for (const n of names) {
		const imgs = sets.map((s) => load(join(fr, s, n)));
		const ref = REF ? load(join(REF, n)) : null;
		const vsRef = imgs.map((im) => (im && ref ? meanAbsDiff(im, ref) : null));
		const ab = imgs.length === 2 && imgs[0] && imgs[1] ? meanAbsDiff(imgs[0], imgs[1]) : null;
		const f = (x) => (x === null ? '-' : x.toFixed(3));
		console.log(`| ${n} | ${vsRef.map(f).join(' | ')} | ${f(ab)} |`);
	}
}
