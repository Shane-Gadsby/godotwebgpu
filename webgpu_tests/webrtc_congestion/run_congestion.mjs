// Orchestrates one congestion run: signaling server + two peers, each on its
// own side of the shaped link, with their output merged into one log.
//
// It must be started INSIDE the shaped namespace, i.e. through shape.sh:
//
//   ./shape.sh --rate 2mbit --delay 25ms -- node run_congestion.mjs
//   ./shape.sh --rate 1mbit --delay 50ms --loss 3% -- \
//       env RTC_TEST=unreliable node run_congestion.mjs
//
// Peers are native by default. RTC_A / RTC_B pick each side independently and
// take native | web | external:
//
//   RTC_A=web RTC_B=native node run_congestion.mjs    # browser offerer
//   RTC_B=external RTC_SIGNAL_IP=192.168.1.29 ...     # peer B is on another
//                                                     # machine, started by hand
//
// Namespace A is this process's own; namespace B is reached with
// `ip netns exec rtcb`. A web peer gets an HTTP server on 127.0.0.1 *inside its
// own namespace*, so only signaling and the data channel cross the shaped link
// -- serving a 42 MB wasm across a 2 Mbit/s link would otherwise cost minutes
// before the measurement even starts.
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, readFileSync, statSync } from 'node:fs';
import { dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, '../..');
const PROJ = join(HERE, 'fixtures/congestion_project');
// shape.sh exports RTC_A_IP, so its presence is how this process knows whether
// it is inside the shaped namespace. Without it, both peers run here on
// loopback: that unshaped control is worth having rather than erroring out,
// because it is what shows the fixtures report NO_PRESSURE when nothing is
// shaping -- i.e. that a shaped TRACKED result is not an artifact of the
// fixture.
// RTC_RATE is exported by shape.sh, so its presence is how this process knows
// it is inside the shaped namespace (and therefore that peer B is reached with
// `ip netns exec`).
const SHAPED = !!process.env.RTC_RATE;
// RTC_SIGNAL_IP is for the off-box procedure: bind signaling to a LAN address
// so a peer on another machine can reach it. See README "Off-box peer".
const A_IP = process.env.RTC_SIGNAL_IP || process.env.RTC_A_IP || '127.0.0.1';
const SIGNAL_PORT = Number(process.env.SIGNAL_PORT || 9099);
const SIGNAL_URL = `ws://${A_IP}:${SIGNAL_PORT}`;
const TEST = process.env.RTC_TEST || 'backpressure';
const A_KIND = (process.env.RTC_A || 'native').toLowerCase();
const B_KIND = (process.env.RTC_B || 'native').toLowerCase();
const EDITOR = process.env.EDITOR_BIN || join(ROOT, 'bin/godot.linuxbsd.editor.x86_64');
const EXPORT_DIR = process.env.RTC_EXPORT_DIR || join(HERE, 'exports/congestion');
const TIMEOUT_MS = Number(process.env.RTC_TIMEOUT || 300000);

const lines = [];
let finished = 0;
const children = [];

function emit(who, text) {
	for (const raw of text.split('\n')) {
		const line = raw.trimEnd();
		if (!line) continue;
		const i = line.indexOf('RTC|');
		if (i >= 0) {
			const payload = line.slice(i);
			lines.push(payload);
			console.log(payload);
			if (/RTC\|( \[B\])? END/.test(payload)) {
				finished++;
			}
		} else if (/\[signal\]|ERROR|SCRIPT ERROR|WARNING|Uncaught|abort|RuntimeError|GDExtension/i.test(line)) {
			// The signaling server already tags its own lines.
			console.log(line.startsWith('[signal]') ? line : `[${who}] ${line}`);
		}
	}
}

// ─── Signaling ────────────────────────────────────────────────────────────────

function startSignaling() {
	const p = spawn(process.execPath, [join(HERE, 'signal_server.mjs')], {
		env: { ...process.env, SIGNAL_HOST: A_IP, SIGNAL_PORT: String(SIGNAL_PORT) },
		stdio: ['ignore', 'pipe', 'pipe'],
	});
	p.stdout.on('data', (d) => emit('signal', d.toString()));
	p.stderr.on('data', (d) => emit('signal', d.toString()));
	children.push(p);
	return p;
}

// ─── Native peer ──────────────────────────────────────────────────────────────

function startNative(role, inNsB) {
	if (!existsSync(EDITOR)) {
		console.error(`[runner] no editor at ${EDITOR} — build one or set EDITOR_BIN`);
		process.exit(2);
	}
	const argv = ['--headless', '--path', PROJ, '--', TEST, role, SIGNAL_URL];
	const useNs = inNsB && SHAPED;
	const cmd = useNs ? 'ip' : EDITOR;
	const args = useNs ? ['netns', 'exec', 'rtcb', EDITOR, ...argv] : argv;
	const p = spawn(cmd, args, { stdio: ['ignore', 'pipe', 'pipe'] });
	p.stdout.on('data', (d) => emit(role, d.toString()));
	p.stderr.on('data', (d) => emit(role, d.toString()));
	children.push(p);
	return p;
}

// ─── Web peer ─────────────────────────────────────────────────────────────────

const MIME = {
	'.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
	'.pck': 'application/octet-stream', '.png': 'image/png', '.json': 'application/json',
};

// Serves the export on 127.0.0.1 inside whichever namespace this runs in, with
// COOP/COEP so a threaded export would work too.
function serveExport() {
	const server = createServer((req, res) => {
		const file = join(EXPORT_DIR, decodeURIComponent(req.url.split('?')[0]));
		if (!existsSync(file) || !statSync(file).isFile()) {
			res.writeHead(404); res.end('not found'); return;
		}
		res.writeHead(200, {
			'Content-Type': MIME[extname(file)] || 'application/octet-stream',
			'Cross-Origin-Opener-Policy': 'same-origin',
			'Cross-Origin-Embedder-Policy': 'require-corp',
		});
		res.end(readFileSync(file));
	});
	return new Promise((r) => server.listen(0, '127.0.0.1',
		() => r({ server, port: server.address().port })));
}

async function loadPlaywright() {
	for (const c of [
		join(HERE, 'node_modules/playwright/index.mjs'),
		join(ROOT, 'webgpu_tests/webrtc_parity/node_modules/playwright/index.mjs'),
		join(ROOT, 'webgpu_tests/screenshot_comparison/node_modules/playwright/index.mjs'),
	]) {
		if (existsSync(c)) return import(pathToFileURL(c).href);
	}
	return import('playwright');
}

async function startWeb(role) {
	if (!existsSync(join(EXPORT_DIR, 'index.html'))) {
		console.error(`[runner] no export in ${EXPORT_DIR} — run ./export.sh first`);
		process.exit(2);
	}
	const { server, port } = await serveExport();
	const pw = await loadPlaywright();
	// --no-sandbox is required, not optional: shape.sh already put us in a user
	// namespace, and Chrome's own sandbox cannot nest inside one.
	const browser = await pw.chromium.launch({
		args: [
			'--no-sandbox', '--disable-dev-shm-usage',
			'--enable-unsafe-webgpu', '--enable-features=Vulkan',
			'--use-gl=angle', '--use-angle=vulkan',
		],
	});
	const page = await browser.newPage();
	page.on('console', (m) => emit(role, m.text()));
	page.on('pageerror', (e) => emit(role, `pageerror: ${e.message}`));
	const url = `http://127.0.0.1:${port}/index.html?test=${encodeURIComponent(TEST)}`
		+ `&role=${role}&signal=${encodeURIComponent(SIGNAL_URL)}`;
	await page.goto(url, { waitUntil: 'domcontentloaded' });
	return {
		kill: async () => {
			try { await browser.close(); } catch { /* already gone */ }
			server.close();
		},
	};
}

// ─── Run ──────────────────────────────────────────────────────────────────────

console.log(`[runner] test=${TEST} A=${A_KIND} B=${B_KIND} signal=${SIGNAL_URL}`);
console.log(`[runner] link: rate=${process.env.RTC_RATE || '?'} delay=${process.env.RTC_DELAY || '?'} loss=${process.env.RTC_LOSS || '?'}`);
if (!SHAPED) {
	// Every number this tier produces is meaningless without shaping, and an
	// unshaped run is the single likeliest mistake — so say so loudly rather
	// than printing a plausible-looking result. backpressure.gd also reports
	// verdict=NO_PRESSURE in that case, independently.
	console.log('[runner] NOTE: not started through shape.sh — this is the UNSHAPED control.');
	console.log('[runner]       both peers run on loopback; expect verdict=NO_PRESSURE.');
}

const webPeers = [];
startSignaling();
await new Promise((r) => setTimeout(r, 500));

// The answerer must be attached before the offerer makes an offer; the server
// gates on that with its "ready" message, so order here is a nicety, not a
// requirement.
if (B_KIND === 'external') {
	// Somebody else starts this peer -- another machine, usually. Print the
	// exact command so the two sides cannot disagree about test or signaling
	// address, which is the whole difficulty of a two-machine run.
	console.log('[runner] peer B is EXTERNAL. Start it now, on the other machine:');
	console.log(`[runner]   ./godot.linuxbsd.editor.x86_64 --headless --path <project> -- ${TEST} answerer ${SIGNAL_URL}`);
	console.log('[runner] waiting for it to join...');
} else if (B_KIND === 'web') {
	webPeers.push(await startWeb('answerer'));
} else {
	startNative('answerer', true);
}
await new Promise((r) => setTimeout(r, 1000));
if (A_KIND === 'external') {
	console.log('[runner] peer A is EXTERNAL. Start it now, on the other machine:');
	console.log(`[runner]   ./godot.linuxbsd.editor.x86_64 --headless --path <project> -- ${TEST} offerer ${SIGNAL_URL}`);
} else if (A_KIND === 'web') {
	webPeers.push(await startWeb('offerer'));
} else {
	startNative('offerer', false);
}

// An external peer has to be started by hand, so give it room to appear.
const EXTERNAL_GRACE_MS = (A_KIND === 'external' || B_KIND === 'external') ? 120000 : 0;

const deadline = Date.now() + TIMEOUT_MS + EXTERNAL_GRACE_MS;
while (finished < 2 && Date.now() < deadline) {
	await new Promise((r) => setTimeout(r, 250));
}
const timedOut = finished < 2;
if (timedOut) {
	console.log(`[runner] TIMEOUT after ${TIMEOUT_MS}ms — ${finished}/2 peers finished`);
}

for (const p of children) {
	try { p.kill('SIGTERM'); } catch { /* already gone */ }
}
for (const w of webPeers) {
	await w.kill();
}

const verdict = lines.filter((l) => / verdict=/.test(l)).pop() || '';
console.log(`\n[runner] ${verdict || 'no verdict printed'}`);
// Exit code reflects whether the run produced a result, not whether the result
// is good news: a CEILING_HIT is a finding, not a harness failure.
process.exit(timedOut || !verdict ? 1 : 0);
