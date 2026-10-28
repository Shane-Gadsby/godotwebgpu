// Node harness for platform/web/js/engine/preloader.js: dedup, failure refetch, mid-body error, progress.
import { createServer } from 'http';
import { readFileSync } from 'fs';
import vm from 'vm';
const SRC = process.argv[2];
globalThis.requestAnimationFrame = (f) => setTimeout(f, 5);
const ctx = vm.createContext(globalThis);
vm.runInContext(readFileSync(SRC, 'utf8') + '\nglobalThis.Preloader = Preloader;', ctx);
const hits = {};
let failA = 0; // fail /a this many times
let cutB = 0; // cut /b mid-body this many times
const srv = createServer((req, res) => {
	hits[req.url] = (hits[req.url] || 0) + 1;
	const body = Buffer.alloc(200000, 7);
	if (req.url === '/a' && failA > 0) { failA--; res.writeHead(500); res.end(); return; }
	res.writeHead(200, { 'Content-Length': body.length });
	if (req.url === '/b' && cutB > 0) { cutB--; res.write(body.subarray(0, 1000)); setTimeout(() => res.destroy(), 50); return; }
	setTimeout(() => res.end(body), 100);
});
await new Promise((r) => srv.listen(8799, '127.0.0.1', r));
const U = (p) => `http://127.0.0.1:8799${p}`;
let fails = 0;
const check = (c, m) => { console.log(`${c ? 'PASS' : 'FAIL'} ${m}`); if (!c) fails++; };

let p = new Preloader();
// 1: two requests, one download, one entry.
const x1 = p.fetchFile(U('/x'), 200000);
await Promise.all([p.preload(U('/x'), 'x.pck', 200000), p.preload(U('/x'), 'x.pck', 200000)]);
await x1;
check(hits['/x'] === 1, `same URL fetched once (hits ${hits['/x']})`);
check(p.preloadedFiles.length === 1, `one preloadedFiles entry (${p.preloadedFiles.length})`);
// 2: progress callback set after the download finished reports the totals at once.
let seen = null;
p.setProgressFunc((l, t) => { seen = [l, t]; });
await new Promise((r) => setTimeout(r, 50));
check(seen && seen[0] === 200000 && seen[1] === 200000, `late progress callback continues from loaded (${seen})`);
// 3: failing file: rejected after retries, then a later request fetches again.
failA = 4;
let rejected = false;
await p.fetchFile(U('/a'), 200000).catch(() => { rejected = true; });
check(rejected && hits['/a'] === 4, `failed download rejected after 4 attempts (${hits['/a']})`);
await p.preload(U('/a'), 'a.pck', 200000);
check(hits['/a'] === 5 && p.preloadedFiles.length === 2, `refetched after failure (${hits['/a']})`);
// 4: connection cut mid-body: retried, not stuck.
cutB = 1;
const t = Date.now();
await p.preload(U('/b'), 'b.pck', 200000);
check(hits['/b'] === 2, `cut body retried (${hits['/b']} requests, ${Date.now() - t} ms)`);
// 5: raw load: bodyLoaded settles after the body.
const r = await p.loadPromise(U('/w'), 200000, true);
await p.bodyLoaded(U('/w'));
const buf = await r.arrayBuffer();
check(buf.byteLength === 200000, `raw body complete (${buf.byteLength})`);
// 6: takeDownload hands over and forgets; releaseDownloads frees.
p.fetchFile(U('/e'), 200000, true);
const taken = p.takeDownload(U('/e'));
check(taken && p.takeDownload(U('/e')) === null, 'takeDownload once');
await taken;
p.fetchFile(U('/f'), 200000);
p.releaseDownloads();
await new Promise((r2) => setTimeout(r2, 300));
p.fetchFile(U('/f'), 200000);
await new Promise((r2) => setTimeout(r2, 300));
check(hits['/f'] === 2, `released downloads are fetched again (${hits['/f']})`);
// 7: prefetch progress includes background files, main progress does not.
p = new Preloader();
let main = null, all = null;
p.setProgressFunc((l, t2) => { main = [l, t2]; });
p.setPrefetchProgressFunc((l, t2) => { all = [l, t2]; });
await Promise.all([p.fetchFile(U('/m1'), 200000), p.fetchFile(U('/bg1'), 200000, true)]);
await new Promise((r2) => setTimeout(r2, 50));
check(main && main[1] === 200000 && all && all[1] === 400000, `main total ${main} / prefetch total ${all}`);
srv.close();
console.log(fails ? `FAILED ${fails}` : 'ALL PASS');
process.exit(fails ? 1 : 0);
