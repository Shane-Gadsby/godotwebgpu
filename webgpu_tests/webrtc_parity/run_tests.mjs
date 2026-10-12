// WebRTC parity tier, CI entry point: runs every fixture in a browser and
// asserts what each one is supposed to prove.
//
// This is the WEB HALF ONLY, on purpose. The native-vs-web diff (compare.sh)
// needs the webrtc-native reference extension, a separate 581 MB clone whose
// built libraries are gitignored, so it cannot run on a fresh checkout or in
// CI. What runs here instead is each fixture's own verdict — which is enough to
// catch a regression in any of the fixes (F1-F5, F7, F8), because every one of
// them has a fixture line that changes if it breaks.
//
//   node run_tests.mjs                           # every export, Chromium
//   RTC_VARIANTS=nothreads node run_tests.mjs    # or threads, dlink, all
//   RTC_BROWSERS=chromium,firefox node run_tests.mjs
//   RTC_FILTER=burst node run_tests.mjs
//
// RTC_BROWSERS defaults to chromium alone, because that is the one browser a
// headless CI runner has software WebGPU for. "safari" is accepted and is
// macOS-only (AppleScript; see run_loopback.mjs).
//
// Exit codes follow local_ci.sh's convention: 0 pass, 1 fail, 2 "cannot run
// here" (no export to test).
import { execFile } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const FILTER = process.env.RTC_FILTER || '';
const TIMEOUT_MS = Number(process.env.RTC_TIMEOUT || 180000);
const BROWSERS = (process.env.RTC_BROWSERS || 'chromium').split(',').map((b) => b.trim()).filter(Boolean);

// What each fixture must and must not print. `want` entries are [description,
// regex] so a failure names the behavior that regressed rather than a pattern.
const EXPECT = {
	loopback: {
		want: [
			['channels open and both round-trips land', /binary rx='binary-round-trip' ok=true/],
			['text mode sets was_string_packet', /text rx='text-round-trip' ok=true was_string=true/],
			['F2: queued packets survive a remote close', /f2 verdict=DRAINED/],
			['F2: the count is not zeroed either', /avail_after_close=1/],
			['F5: unset reliability reads -1, as native reports it', /open ch1 .*lifetime=-1 rtx=-1/],
			['F7: pre-open id is native\'s 65535 sentinel', /pre_open ch1 .*id=65535/],
		],
	},
	sizes: {
		// F1 + F3: every payload up to 256 KB must arrive intact.
		want: [
			['1200 bytes delivered', /size=1200 rx_len=1200 intact=true verdict=DELIVERED/],
			['16385 bytes delivered (over native\'s stated max)', /size=16385 rx_len=16385 intact=true verdict=DELIVERED/],
			['F3: 65531 bytes delivered (over the old 64 KB buffer)', /size=65531 rx_len=65531 intact=true verdict=DELIVERED/],
			['F3: 262144 bytes delivered', /size=262144 rx_len=262144 intact=true verdict=DELIVERED/],
		],
		deny: [['no payload is dropped or refused', /verdict=(DROPPED|NOT_SENT)/]],
	},
	mesh: {
		want: [
			['mesh connects and disconnect is seen', /verdict connected_both=true disconnect_seen=true/],
			['reliable mode delivers', /recv mode=RELIABLE .*ok=true/],
			['unreliable-ordered mode delivers', /recv mode=UNRELIABLE_ORDERED .*ok=true/],
			['unreliable mode delivers', /recv mode=UNRELIABLE .*ok=true/],
			['F8: the ordered channel really carries a packet lifetime', /ch1 .*lifetime=1 .*label=ordered/],
			['F8: so does the unreliable channel', /ch2 .*lifetime=1 .*label=unreliable/],
		],
		deny: [['no mode times out', /recv mode=\w+ TIMEOUT/]],
	},
	mesh_close: {
		want: [
			['packets are queued before the close', /mp_queued_before_close=3/],
			['F2 at the multiplayer layer: the channel still drains', /ch_state=\d+ ch_avail_after_disconnect=3 ch_drain_len=13 .*verdict=DRAINED/],
		],
	},
	burst: {
		want: [
			['F3: 500 packets in one tick, none lost', /drained=500 of 500 sent \(loss=0\) out_of_order=0/],
			['F3 verdict', /burst verdict=LOSSLESS/],
		],
	},
	grow: {
		want: [
			['F3: wrapped-buffer growth loses, duplicates and reorders nothing', /sent=2100 received=2100 loss=0 out_of_order=0 dupes=0/],
			['F3 growth verdict', /grow verdict=LOSSLESS/],
		],
	},
	buffered: {
		want: [
			['get_buffered_amount stays in range and settles at 0', /in_range=true \(out_of_range=0 samples\)/],
			['everything arrives in order', /final arrived=true in_order=true buffered_settled=true/],
			['verdict', /buffered verdict=CONSISTENT/],
		],
	},
	rpc: {
		want: [
			['MultiplayerAPI mesh connects on both sides', /peers api1=\[2\] api2=\[1\]/],
			['reliable @rpc arrives with the right sender id', /recv ping from=1 payload='hello-reliable' mode=reliable/],
			['unreliable_ordered @rpc arrives', /recv ping from=1 payload='hello-uo' mode=unreliable_ordered/],
			['unreliable @rpc arrives', /recv ping from=1 payload='hello-u' mode=unreliable/],
			['broadcast @rpc arrives', /recv ping from=1 payload='hello-broadcast'/],
			['the reverse @rpc arrives with the right sender id', /recv pong from=2 payload='hello-back'/],
			['all three transfer modes and both directions', /rpc pings=4 sender_ids_ok=true modes=3/],
			['verdict', /rpc verdict=OK/],
		],
		deny: [['no RPC step times out', /rpc step=\d+ TIMEOUT/]],
	},
	chancfg: {
		want: [
			['F8: the typo spelling is still dropped by the transport', /typo local  ordered=true lifetime=-1 rtx=-1/],
			['F8: the correct spelling is applied', /correct local  ordered=true lifetime=1/],
			['F5: a set maxRetransmits=0 is distinguishable from unset', /rtx0 local  ordered=false lifetime=-1 rtx=0/],
			['F5: unset reads -1 on both axes', /baseline local  ordered=true lifetime=-1 rtx=-1/],
			['verdict', /chancfg verdict=TYPO_DROPPED/],
		],
	},
};

// Every fixture must finish and none may time out.
const UNIVERSAL_WANT = [['fixture ran to completion', /RTC\| END/]];
const UNIVERSAL_DENY = [
	['no fixture-level timeout', /RTC\| TIMEOUT/],
	['no unknown test name', /FATAL unknown test/],
	['no engine error', /\bERROR:/],
	['no script error', /SCRIPT ERROR/],
];

const variants = [
	['nothreads', join(HERE, 'exports/loopback')],
	['threads', join(HERE, 'exports/loopback_threads')],
	['dlink', join(HERE, 'exports/loopback_dlink')],
].filter(([name, dir]) => {
	if (!existsSync(join(dir, 'index.html'))) {
		return false;
	}
	const want = process.env.RTC_VARIANTS;
	return !want || want === 'both' || want === 'all' || want === name;
});

if (variants.length === 0) {
	console.log('SKIP -- no export in exports/; run ./export.sh first');
	process.exit(2);
}

function runFixture(dir, test, browser) {
	return new Promise((resolve) => {
		execFile(process.execPath, [join(HERE, 'run_loopback.mjs')], {
			cwd: HERE,
			timeout: TIMEOUT_MS,
			maxBuffer: 32 * 1024 * 1024,
			env: {
				...process.env, RTC_DIR: dir, RTC_TEST: test, RTC_BROWSER: browser,
				RTC_TIMEOUT: String(TIMEOUT_MS - 20000),
			},
		}, (err, stdout, stderr) => resolve({ err, out: `${stdout}\n${stderr}` }));
	});
}

let failures = 0;
let checks = 0;
const skipped = [];

for (const browser of BROWSERS) {
for (const [variant, dir] of variants) {
	// CI exports a single template of its choosing (RTC_TEMPLATE_ZIP) into
	// exports/loopback, so the directory name does not reliably say what the
	// build is. Report the side module's presence instead of trusting the label.
	const hasSide = existsSync(join(dir, 'index.side.wasm'));
	const kind = hasSide && variant !== 'dlink' ? `${variant}, dlink template` : variant;
	console.log(`\n=== ${browser} / ${kind} (${dir})`);
	let browserUnavailable = false;
	for (const [test, spec] of Object.entries(EXPECT)) {
		if (FILTER && !test.includes(FILTER)) {
			continue;
		}
		if (browserUnavailable) {
			continue;
		}
		const { out } = await runFixture(dir, test, browser);
		// A browser that cannot launch, or has no WebGPU adapter to boot the
		// engine on, is a missing prerequisite rather than a parity failure --
		// Firefox has no software WebGPU on Linux, so this is the expected
		// outcome on a GPU-less runner. Report it as a skip once, not as nine
		// identical failures.
		if (!/RTC\|/.test(out) && /browserType\.launch|Executable doesn't exist|needs macOS|No GPU adapter|WebGPU is not available/i.test(out)) {
			const why = (out.match(/^.*(browserType\.launch|Executable doesn't exist|needs macOS|No GPU adapter|WebGPU is not available).*$/mi) || [''])[0].trim();
			console.log(`  SKIP  ${browser} unavailable: ${why.slice(0, 120)}`);
			skipped.push(`${browser}/${variant}`);
			browserUnavailable = true;
			continue;
		}
		const problems = [];
		for (const [what, re] of [...UNIVERSAL_WANT, ...(spec.want || [])]) {
			checks++;
			if (!re.test(out)) {
				problems.push(`missing: ${what}  [${re}]`);
			}
		}
		for (const [what, re] of [...UNIVERSAL_DENY, ...(spec.deny || [])]) {
			checks++;
			const m = out.match(re);
			if (m) {
				problems.push(`violated: ${what}  [matched "${m[0].trim()}"]`);
			}
		}
		if (problems.length === 0) {
			console.log(`  PASS  ${test}`);
		} else {
			failures += problems.length;
			console.log(`  FAIL  ${test}`);
			for (const p of problems) {
				console.log(`        ${p}`);
			}
			// The fixture's own output is the only useful diagnostic here.
			for (const line of out.split('\n').filter((l) => l.includes('RTC|') || l.includes('[browser-err]'))) {
				console.log(`        | ${line.trim()}`);
			}
		}
	}
}
}

// Zero assertions is not a pass. Without this a run where every browser was
// unavailable exits 0 and the suite reports coverage it does not have --
// local_ci.sh's run_test() reads exit 2 plus a "SKIP -- " line as "cannot run
// here", which is what this actually is.
if (checks === 0) {
	console.log(`\nSKIP -- no browser could run a fixture (${skipped.join(', ') || 'nothing attempted'})`);
	process.exit(2);
}

console.log(`\n${failures === 0 ? 'PASS' : 'FAIL'}: ${checks} assertions, ${failures} failed`);
if (skipped.length > 0) {
	console.log(`skipped (browser unavailable): ${skipped.join(', ')}`);
}
console.log('(web half only — native-vs-web parity needs ./compare.sh and the reference extension)');
process.exit(failures === 0 ? 0 : 1);
