/*
CPU-profile the main thread of an already open Chrome tab and report ms/frame per JS function.
Written to measure the Web Audio sample driver (AudioParam writes in library_godot_audio.js), but it
profiles any function by name. Zero dependencies, Node 22+ (built-in fetch and WebSocket). It never
launches or serves anything: you host the export and open it yourself.

1. Start Chrome with a debugging port. Since Chrome 136 the port is ignored on the default profile,
   so a separate user-data-dir is required:
     google-chrome --remote-debugging-port=9222 --user-data-dir=/tmp/godot-profile
2. Open the exported project in that Chrome and let it run.
3. Close DevTools on that tab if it is open (two profiler clients on one target conflict), then:
     node tests/web_audio_profiling.mjs --tab platformer --fn setVolume,_syncPlaybackRate

Options (defaults in brackets):
  --port N        remote debugging port [9222]
  --tab TEXT      substring of the tab's URL or title; first page target that matches [first page]
  --settle S      seconds to wait before profiling so the scene is in a steady state [35]
  --measure S     seconds to profile [8]
  --interval US   sampling interval in microseconds [200]
  --fn A,B,C      function names to report, substring match [setVolume,_syncPlaybackRate]
                  (upstream master's driver; the closure-based driver from PR #123996 that this fork
                  carries names them setGain and setPitchScale)
  --top N         also list the N functions with the most self time [15]

Frames are counted with a requestAnimationFrame loop injected into the page, which for a Godot web
export rendering on the main thread is one count per engine frame. "Self" is time spent in the
function's own body, "incl" includes everything it called. Both are divided by the frames rendered
during the profile, so the numbers are ms per frame.
*/

let args = process.argv.slice(2),
	opt = (name, def) => {
		let i = args.indexOf(`--${name}`)
		return i !== -1 && i + 1 < args.length ? args[i + 1] : def
	},
	PORT = +opt('port', 9222),
	TAB = opt('tab', ''),
	SETTLE = +opt('settle', 35) * 1000,
	MEASURE = +opt('measure', 8) * 1000,
	INTERVAL = +opt('interval', 200),
	FNS = opt('fn', 'setVolume,_syncPlaybackRate').split(',').map((s) => s.trim()).filter(Boolean),
	TOP = +opt('top', 15),
	sleep = (ms) => new Promise((r) => setTimeout(r, ms)),
	nextId = 0,
	targets,
	pages,
	target,
	ws,
	pending,
	send = (method, params = {}) => new Promise((res, rej) => {
		let id = ++nextId
		pending.set(id, {res, rej})
		ws.send(JSON.stringify({id, method, params}))
	}),
	evaluate = async (expression) => (await send('Runtime.evaluate', {expression, returnByValue: true})).result.value,
	keyOf = (n) => {
		let cf = n.callFrame,
			file = cf.url ? cf.url.split('/').pop().split('?')[0] : ''
		return `${cf.functionName || '(anonymous)'}${file ? `  ${file}:${cf.lineNumber + 1}` : ''}`
	},
	metrics = async () => Object.fromEntries((await send('Performance.getMetrics')).metrics.map((m) => [m.name, m.value]))

//Find the tab. Chrome may bind the port to IPv4 or IPv6 localhost depending on the machine.
for (let host of ['localhost', '127.0.0.1', '[::1]']) {
	try {
		targets = await (await fetch(`http://${host}:${PORT}/json/list`)).json()
		break
	}
	catch { /* try the next one */
	}
}

if (!targets) {
	console.error(`Could not reach Chrome on port ${PORT}.\nStart it with --remote-debugging-port=${PORT} --user-data-dir=/tmp/godot-profile`)
	process.exit(1)
}

pages = targets.filter((t) => t.type === 'page')
target = pages.find((t) => !TAB || t.url.includes(TAB) || (t.title || '').includes(TAB))

if (!target) {
	console.error(`No open tab matches "${TAB}". Open tabs:\n` + pages.map((t) => `  ${t.title}  ${t.url}`).join('\n'))
	process.exit(1)
}

console.log(`Profiling: ${target.title}  ${target.url}`)

//Minimal CDP client.
ws = new WebSocket(target.webSocketDebuggerUrl)
await new Promise((res, rej) => {
	ws.addEventListener('open', res)
	ws.addEventListener('error', rej)
})

pending = new Map()
ws.addEventListener('message', (ev) => {
	let m = JSON.parse(ev.data)
	if (m.id && pending.has(m.id)) {
		let {res, rej} = pending.get(m.id)
		pending.delete(m.id)
		m.error ? rej(new Error(`${m.error.message} (${m.error.code})`)) : res(m.result)
	}
})

await send('Runtime.enable')
await send('Performance.enable')
await send('Profiler.enable')
await send('Profiler.setSamplingInterval', {interval: INTERVAL})


//Frame counter.
await evaluate(`
	(() => {
		if (!window.__profFrames) {
			window.__profFrames = {n: 0}
			let tick = () => {
				window.__profFrames.n++
				requestAnimationFrame(tick)
			}
			requestAnimationFrame(tick)
		}
		return true
	})()
`)

if (SETTLE > 0) {
	console.log(`Settling for ${SETTLE / 1000} s...`)
	await sleep(SETTLE)
}


console.log(`Profiling for ${MEASURE / 1000} s at ${INTERVAL} us sampling...`)
let frames0 = await evaluate('window.__profFrames.n'),
	met0 = await metrics()
await send('Profiler.start')
await sleep(MEASURE)
let {profile} = await send('Profiler.stop'),
	frames1 = await evaluate('window.__profFrames.n'),
	met1 = await metrics()
ws.close()

let frames = frames1 - frames0,
	wall = met1.Timestamp - met0.Timestamp,
	taskMs = (met1.TaskDuration - met0.TaskDuration) * 1000

if (frames === 0) {
	console.error('No frames were rendered during the window, is the tab visible and running?')
	process.exit(1)
}

//Aggregate the sampling profile.
let nodes = new Map(profile.nodes.map((n) => [n.id, n])),
	parent = new Map()

for (const n of profile.nodes) {
	for (const c of n.children || []) {
		parent.set(c, n.id)
	}
}

let self = new Map(),
	incl = new Map(),
	busyUs = 0, totalUs = 0

for (let i = 0; i < profile.samples.length; i++) {
	let dt = profile.timeDeltas[i] || 0,
		id = profile.samples[i],
		node = nodes.get(id),
		fn = node.callFrame.functionName,
		k = keyOf(node),
		seen = new Set()

	totalUs += dt
	if (fn === '(idle)') {
		continue
	}

	busyUs += dt
	self.set(k, (self.get(k) || 0) + dt)

	for (; id !== undefined; id = parent.get(id)) {
		let kk = keyOf(nodes.get(id))
		if (seen.has(kk)) {
			continue
		}
		seen.add(kk)
		incl.set(kk, (incl.get(kk) || 0) + dt)
	}
}
//The profile's own span differs slightly from the measurement window; scale to engine frames over that span.
let fps = frames / wall,
	framesInProfile = fps * (totalUs / 1e6),
	perFrame = (us) => (us / 1000 / framesInProfile).toFixed(3).padStart(8)

console.log('');
console.log(`frames ${frames} in ${wall.toFixed(2)} s  (${fps.toFixed(1)} fps)`);
console.log(`main thread busy: ${(busyUs / 1000 / framesInProfile).toFixed(3)} ms/frame from the profile, ${(taskMs / frames).toFixed(3)} ms/frame from Performance.getMetrics TaskDuration`);
console.log('');
console.log('Requested functions (ms/frame):');
console.log('    self     incl  function');

let matched = [...incl.keys()].filter((k) => FNS.some((f) => k.split('  ')[0].includes(f))).sort((a, b) => (self.get(b) || 0) - (self.get(a) || 0));
if (matched.length === 0) {
	console.log('  (none of ' + FNS.join(', ') + ' were sampled)');
}

for (let k of matched) {
	console.log(`${perFrame(self.get(k) || 0)} ${perFrame(incl.get(k))}  ${k}`);
}

let matchedSelf = matched.reduce((s, k) => s + (self.get(k) || 0), 0);
if (matched.length) {
	console.log(`${perFrame(matchedSelf)}           total self, ${(100 * matchedSelf / busyUs).toFixed(1)}% of main-thread busy time`);
}

if (TOP > 0) {
	console.log('')
	console.log(`Top ${TOP} by self time (ms/frame):`)
	console.log('    self     incl  function')
	for (let [k, us] of [...self.entries()].sort((a, b) => b[1] - a[1]).slice(0, TOP)) {
		console.log(`${perFrame(us)} ${perFrame(incl.get(k))}  ${k}`)
	}
}
