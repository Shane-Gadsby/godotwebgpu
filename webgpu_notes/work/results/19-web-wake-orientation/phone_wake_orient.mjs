// Task 19 phone check (Razr 60, Chrome, over adb): keep-screen-on and the fullscreen orientation lock.
//
//   node phone_wake_orient.mjs --out <dir> --export <name>=<export dir> [--export ...] [--wake <name>] [--idle 60]
//
// For each export: opens it in a new Chrome tab (adb reverse, as webgpu_tests/perf_guard/android), reads the probe the
// test project leaves on window.__wake_orient_probe, taps the screen (the export's html/fullscreen_on_first_tap enters
// fullscreen; an export named NoFirstTap has it off and must stay windowed), records screen.orientation.type and a screencap, taps
// again (fullscreen must stay). Then, on the --wake export, leaves the phone untouched for --idle seconds (longer than the
// system screen timeout) and samples mWakefulness: it must stay Awake. Last, closes the tab and samples again: the
// screen must go to sleep. Console errors/warnings of every tab are recorded. Exit 1 when a check fails.
// Needs the test project (wake_proj/) exported with the template under test; serves it with COOP/COEP itself.
import { execFileSync } from 'child_process';
import { createServer } from 'http';
import { existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from 'fs';
import { extname, join, resolve } from 'path';
import { CDP, sleep } from '../../../../webgpu_tests/perf_guard/android/cdp.mjs';

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const OUT = resolve(opt('--out', 'phone_out'));
const exportsList = [];
for (let i = 0; i < args.length; i++) if (args[i] === '--export') { const [n, d] = args[i + 1].split('='); exportsList.push({ name: n, dir: resolve(d) }); }
const WAKE = opt('--wake', exportsList[0]?.name);
const IDLE = +opt('--idle', 60);
const PORT = 8099;
const DISPLAY = '4627039422300187648'; // main display of the foldable (memory: android-phone-profiling)
mkdirSync(OUT, { recursive: true });

const adb = (...x) => execFileSync('adb', x, { encoding: 'utf8', maxBuffer: 64 << 20 });
const adbBuf = (...x) => execFileSync('adb', x, { maxBuffer: 64 << 20 });
const log = (...m) => console.log(new Date().toISOString().slice(11, 19), ...m);
const wakefulness = () => (/mWakefulness=(\w+)/.exec(adb('shell', 'dumpsys', 'power')) || [])[1];
const keyguard = () => /mIsShowing=true|mDreamingLockscreen=true|isKeyguardShowing=true|mShowingLockscreen=true/.test(adb('shell', 'dumpsys', 'window', 'policy'));
const keepScreenOnWindows = () => adb('shell', 'dumpsys', 'window', 'windows').split('\n').filter((l) => /KEEP_SCREEN_ON/.test(l)).length;
const screencap = (file) => writeFileSync(join(OUT, file), adbBuf('exec-out', 'screencap', '-d', DISPLAY, '-p'));

const report = { started: new Date().toISOString(), screen_off_timeout_ms: +adb('shell', 'settings', 'get', 'system', 'screen_off_timeout').trim(), exports: {}, failures: [] };
const fail = (m) => { report.failures.push(m); log('FAIL', m); };

let current = null; // the export being served
const servers = Object.fromEntries(exportsList.map((e) => [e.name, e.dir]));
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.png': 'image/png', '.json': 'application/json', '.svg': 'image/svg+xml' };
const server = createServer((q, r) => {
	const p = decodeURIComponent(q.url.split('?')[0]);
	const f = join(servers[current] || '/nonexistent', p === '/' ? 'index.html' : p);
	if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
	const body = readFileSync(f);
	r.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Content-Length': body.length,
		'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp', 'Cache-Control': 'no-store' });
	r.end(body);
});
await new Promise((res) => server.listen(PORT, '127.0.0.1', res));

adb('reverse', `tcp:${PORT}`, `tcp:${PORT}`);
adb('forward', 'tcp:9222', 'localabstract:chrome_devtools_remote');
adb('shell', 'input', 'keyevent', 'KEYCODE_WAKEUP');
await sleep(1000);
if (keyguard()) {
	log('the phone shows its lock screen: stopping (unlock it, then run again)');
	process.exit(2);
}

async function openTab(name) {
	current = name;
	const url = `http://localhost:${PORT}/index.html?t=${Date.now()}`;
	adb('shell', 'am', 'start', '-a', 'android.intent.action.VIEW', '-d', url, '-e', 'com.android.browser.application_id', 'task19_' + name, 'com.android.chrome');
	let target = null;
	for (let i = 0; i < 40 && !target; i++) {
		await sleep(500);
		try {
			adb('forward', 'tcp:9222', 'localabstract:chrome_devtools_remote');
			const list = await (await fetch('http://127.0.0.1:9222/json/list')).json();
			target = list.find((t) => t.type === 'page' && t.url === url);
		} catch { /* devtools socket not up yet */ }
	}
	if (!target) throw new Error('tab did not appear');
	const cdp = await CDP.attach(target.webSocketDebuggerUrl);
	const consoleLines = [];
	cdp.on('Runtime.consoleAPICalled', (p) => consoleLines.push({ type: p.type, text: p.args.map((a) => a.value ?? a.description ?? '').join(' ') }));
	cdp.on('Runtime.exceptionThrown', (p) => consoleLines.push({ type: 'exception', text: p.exceptionDetails.exception?.description || p.exceptionDetails.text }));
	cdp.on('Log.entryAdded', (p) => consoleLines.push({ type: 'log-' + p.entry.level, text: p.entry.text }));
	await cdp.send('Runtime.enable');
	await cdp.send('Log.enable');
	await cdp.send('Page.reload'); // the probe and the console lines from the start; the engine boots once, after this
	return { cdp, target, consoleLines };
}

async function waitProbe(cdp) {
	for (let i = 0; i < 120; i++) {
		await sleep(1000);
		try {
			const p = await cdp.eval('window.__wake_orient_probe ? JSON.stringify(window.__wake_orient_probe) : null');
			if (p) return JSON.parse(p);
		} catch { /* navigating */ }
	}
	return null;
}

const state = (cdp) => cdp.eval(`JSON.stringify({ type: screen.orientation && screen.orientation.type, fullscreen: !!document.fullscreenElement,
	w: innerWidth, h: innerHeight, visibility: document.visibilityState, wakeLockApi: !!navigator.wakeLock })`).then(JSON.parse);
const tapCenter = () => {
	const m = /Physical size: (\d+)x(\d+)/.exec(adb('shell', 'wm', 'size'));
	adb('shell', 'input', 'tap', String(Math.floor(m[1] / 2)), String(Math.floor(m[2] / 2)));
};

let wakeTab = null;
for (const e of exportsList) {
	log(`== ${e.name} (${e.dir})`);
	const tab = await openTab(e.name);
	const probe = await waitProbe(tab.cdp);
	const r = { probe, before: await state(tab.cdp) };
	log('probe', JSON.stringify(probe), 'state', JSON.stringify(r.before));
	if (!probe) fail(`${e.name}: no probe from the page`);
	await sleep(2000);
	tapCenter(); // html/fullscreen_on_first_tap: the shell enters fullscreen on the end of this tap
	await sleep(3000);
	r.fullscreen = await state(tab.cdp);
	log('after tap', JSON.stringify(r.fullscreen));
	screencap(`${e.name}_fullscreen.png`);
	const wantFullscreen = !/NoFirstTap/.test(e.name); // html/fullscreen_on_first_tap off: a tap must do nothing
	if (r.fullscreen.fullscreen !== wantFullscreen) fail(`${e.name}: fullscreen after the first tap is ${r.fullscreen.fullscreen}, expected ${wantFullscreen}`);
	const wantType = { Landscape: 'landscape', Portrait: 'portrait' }[e.name];
	if (wantType && !String(r.fullscreen.type).startsWith(wantType)) fail(`${e.name}: orientation ${r.fullscreen.type} in fullscreen`);
	tapCenter();
	await sleep(3000);
	r.after = await state(tab.cdp);
	log('after second tap', JSON.stringify(r.after));
	screencap(`${e.name}_windowed.png`);
	r.console = tab.consoleLines;
	report.exports[e.name] = r;
	if (e.name === WAKE) {
		wakeTab = tab;
	} else {
		await fetch(`http://127.0.0.1:9222/json/close/${tab.target.id}`);
		tab.cdp.close();
	}
}

if (wakeTab) {
	await fetch(`http://127.0.0.1:9222/json/activate/${wakeTab.target.id}`); // the tab must be visible to hold the lock
	await sleep(2000);
	log('wake tab state', JSON.stringify(await state(wakeTab.cdp)));
	log(`== wake lock: ${IDLE} s untouched, screen timeout ${report.screen_off_timeout_ms} ms`);
	const samples = [];
	for (let t = 0; t <= IDLE; t += 5) {
		samples.push({ t, wakefulness: wakefulness(), keep_screen_on_windows: keepScreenOnWindows() });
		log(JSON.stringify(samples.at(-1)));
		await sleep(5000);
	}
	report.wake_samples = samples;
	screencap('wake_after_idle.png');
	if (samples.some((s) => s.wakefulness !== 'Awake')) fail('the screen went off with the export running');
	report.wake_console = wakeTab.consoleLines;
	if (args.includes('--keep-open')) {
		log('== --keep-open: leaving the wake tab open (it keeps the screen on, so the phone does not lock)');
		report.finished = new Date().toISOString();
		writeFileSync(join(OUT, 'report.json'), JSON.stringify(report, null, 1));
		log(report.failures.length ? `FAILED: ${report.failures.length}` : 'OK');
		process.exit(report.failures.length ? 1 : 0);
	}
	log('== closing the tab: the screen must go off now');
	await fetch(`http://127.0.0.1:9222/json/close/${wakeTab.target.id}`);
	wakeTab.cdp.close();
	const after = [];
	for (let t = 0; t <= IDLE; t += 5) {
		after.push({ t, wakefulness: wakefulness(), keep_screen_on_windows: keepScreenOnWindows() });
		log(JSON.stringify(after.at(-1)));
		if (after.at(-1).wakefulness !== 'Awake') break;
		await sleep(5000);
	}
	report.after_close_samples = after;
	if (after.at(-1).wakefulness === 'Awake') fail('the screen stayed on after the tab was closed');
}

report.finished = new Date().toISOString();
writeFileSync(join(OUT, 'report.json'), JSON.stringify(report, null, 1));
log(report.failures.length ? `FAILED: ${report.failures.length}` : 'OK');
server.close();
process.exit(report.failures.length ? 1 : 0);
