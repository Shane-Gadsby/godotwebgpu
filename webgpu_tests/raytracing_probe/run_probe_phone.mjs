#!/usr/bin/env node
// Runs the probe on an Android phone over USB, the perf_guard/android way: serves it on a fixed port,
// `adb reverse` so http://localhost:<port>/ on the device is a secure context, opens it in the device's
// Chrome with a VIEW intent, reads window.__probeResult back over raw CDP (adb forward of Chrome's
// devtools socket), closes only the tab it opened, and prints the same table as run_probe.mjs.
//
//   node run_probe_phone.mjs [--serial ZY22LHCBJ6] [--port 8099] [--w 1280] [--h 720] [--tris 100000]
//                            [--iters 10] [--verify 32] [--shot out.png] [--display <id>] [--keep-tab]
//
// Exit codes as run_probe.mjs: 0 checks passed, 1 a check failed or the probe errored.
import { createServer } from 'http';
import { readFileSync, mkdirSync, writeFileSync, existsSync } from 'fs';
import { dirname, join, extname } from 'path';
import { fileURLToPath } from 'url';
import { execFileSync } from 'child_process';

const __dirname = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const opt = (name, def) => {
	const i = args.indexOf(`--${name}`);
	return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
};
const flag = (name) => args.includes(`--${name}`);
const serial = opt('serial', null);
const port = +opt('port', '8099');
const w = opt('w', '1280');
const h = opt('h', '720');
const tris = opt('tris', '100000');
const iters = opt('iters', '10');
const verify = opt('verify', '32');
const shot = opt('shot', null);
const display = opt('display', null);
const adbBase = serial ? ['-s', serial] : [];
const adb = (...a) => execFileSync('adb', [...adbBase, ...a], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const types = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.json': 'application/json' };
const server = createServer((req, res) => {
	const path = req.url.split('?')[0];
	const file = join(__dirname, path === '/' ? 'index.html' : path.replace(/^\/+/, ''));
	if (!file.startsWith(__dirname) || !existsSync(file)) {
		res.writeHead(404);
		res.end('not found');
		return;
	}
	res.writeHead(200, { 'Content-Type': types[extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
	res.end(readFileSync(file));
});
await new Promise((r) => server.listen(port, '127.0.0.1', r));
const path = `/?tris=${tris}&w=${w}&h=${h}&iters=${iters}&verify=${verify}&phone=1`;
const url = `http://localhost:${port}${path}`;

const devices = adb('devices').split('\n').slice(1).filter((l) => l.trim().endsWith('device'));
if (!devices.length) {
	console.error('no device attached (adb devices)');
	process.exit(1);
}
console.log(`device: ${devices[0].split('\t')[0]}`);
adb('reverse', `tcp:${port}`, `tcp:${port}`);
// Make sure Chrome is running, then open the tab through CDP: a VIEW intent does not reliably create a
// tab when the phone is locked or Chrome is in the background, and Chrome 155 answers /json/new with
// "Could not create new page" (webgpu_notes/TASKS.md §2.9), so Target.createTarget on the browser socket it is.
adb('shell', 'am start -n com.android.chrome/com.google.android.apps.chrome.Main');
let browserWs = null;
for (let attempt = 0; attempt < 30 && !browserWs; attempt++) {
	try {
		adb('forward', 'tcp:9222', 'localabstract:chrome_devtools_remote'); // dies with Chrome; re-run every time
		browserWs = (await (await fetch('http://127.0.0.1:9222/json/version')).json()).webSocketDebuggerUrl;
	} catch (e) {
		await sleep(1000);
	}
}
if (!browserWs) {
	console.error('Chrome\'s devtools socket did not answer (adb forward tcp:9222 localabstract:chrome_devtools_remote)');
	process.exit(1);
}
const createdId = await new Promise((resolve, reject) => {
	const bws = new WebSocket(browserWs);
	bws.onerror = reject;
	bws.onopen = () => bws.send(JSON.stringify({ id: 1, method: 'Target.createTarget', params: { url } }));
	bws.onmessage = (ev) => {
		const msg = JSON.parse(ev.data);
		if (msg.id === 1) {
			bws.close();
			msg.error ? reject(new Error(msg.error.message)) : resolve(msg.result.targetId);
		}
	};
});
console.log(`opened ${url} in the device's Chrome (target ${createdId})`);
let target = null;
for (let attempt = 0; attempt < 20 && !target; attempt++) {
	const list = await (await fetch('http://127.0.0.1:9222/json/list')).json();
	target = list.find((t) => t.id === createdId) || list.find((t) => t.type === 'page' && t.url.startsWith(`http://localhost:${port}/`));
	if (!target) {
		await sleep(500);
	}
}
if (!target) {
	console.error('the created tab never showed up in /json/list');
	process.exit(1);
}

const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
	ws.onopen = resolve;
	ws.onerror = reject;
});
let nextId = 1;
const pending = new Map();
ws.onmessage = (ev) => {
	const msg = JSON.parse(ev.data);
	if (msg.id && pending.has(msg.id)) {
		pending.get(msg.id)(msg);
		pending.delete(msg.id);
	}
};
const send = (method, params = {}) => new Promise((resolve) => {
	const id = nextId++;
	pending.set(id, resolve);
	ws.send(JSON.stringify({ id, method, params }));
});
const evaluate = async (expression) => {
	const r = await send('Runtime.evaluate', { expression, returnByValue: true });
	return r.result && r.result.result ? r.result.result.value : undefined;
};

const started = Date.now();
let done = false;
while (!done && Date.now() - started < 15 * 60 * 1000) {
	done = (await evaluate('window.__probeDone === true')) === true;
	if (!done) {
		const last = await evaluate("(document.getElementById('log') || {}).textContent || ''");
		const lines = String(last).trim().split('\n');
		if (lines.length) {
			process.stdout.write(`\r  ${lines[lines.length - 1].slice(0, 110).padEnd(110)}`);
		}
		await sleep(2000);
	}
}
process.stdout.write('\n');
if (!done) {
	console.error('timed out waiting for the probe');
	process.exit(1);
}
const result = JSON.parse(await evaluate('JSON.stringify(window.__probeResult)'));
const logText = await evaluate("(document.getElementById('log') || {}).textContent || ''");
console.log(String(logText).trim());

if (shot) {
	const dispArgs = display ? ['-d', display] : [];
	const png = execFileSync('adb', [...adbBase, 'exec-out', 'screencap', ...dispArgs, '-p'], { maxBuffer: 64 * 1024 * 1024 });
	writeFileSync(shot, png);
	console.log(`screenshot: ${shot} (${png.length} bytes)`);
}
if (!flag('keep-tab')) {
	try {
		await fetch(`http://127.0.0.1:9222/json/close/${target.id}`);
	} catch (e) {
		// Leave it; the user's other tabs are untouched either way.
	}
}
ws.close();
server.close();

mkdirSync(join(__dirname, 'results'), { recursive: true });
const stamp = new Date().toISOString().replace(/[:.]/g, '-');
writeFileSync(join(__dirname, 'results', `phone-${stamp}.json`), JSON.stringify(result, null, 2));
writeFileSync(join(__dirname, 'results', 'phone-latest.json'), JSON.stringify(result, null, 2));

if (result.error) {
	console.error(`\nPROBE ERROR: ${result.error}`);
	process.exit(1);
}
const a = result.adapter || {};
console.log(`\nadapter: ${a.vendor} / ${a.architecture} ${a.description}${a.isFallbackAdapter ? '  (FALLBACK ADAPTER -- software)' : ''}`);
console.log(`resolution ${result.resolution.join('x')}, ${result.iters} timed dispatches per kernel, time = median GPU timestamp (wall clock when no timestamp-query)\n`);
console.log('| triangles | BVH build | primary ms / Mrays | shadow ms / Mrays | bounce ms / Mrays | 1 spp frame | traversal vs brute force |');
console.log('|---|---|---|---|---|---|---|');
for (const s of result.scenes) {
	const f = (k) => `${s[k].ms.toFixed(2)} / ${s[k].mrays_per_s.toFixed(0)}`;
	const v = s.verify ? `${s.verify.samples - s.verify.mismatches}/${s.verify.samples} rays agree` : 'not checked';
	console.log(`| ${s.triangles.toLocaleString()} | ${s.build_ms.toFixed(0)} ms | ${f('primary')} | ${f('shadow')} | ${f('bounce')} | ${s.frame_ms_1spp.toFixed(2)} ms | ${v} |`);
}
const passed = (result.checks || []).filter((c) => c.pass).length;
console.log(`\nchecks: ${passed}/${(result.checks || []).length} passed${result.ok ? '' : '  -- FAILED'}`);
for (const c of result.checks || []) {
	if (!c.pass) {
		console.log(`  FAIL [${c.scene}] ${c.name}: ${c.detail}`);
	}
}
process.exit(result.ok ? 0 : 1);
