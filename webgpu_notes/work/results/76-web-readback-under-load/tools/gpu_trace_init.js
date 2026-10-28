// Injected before the page loads: tracks every GPUBuffer's map state and reports submits that use a buffer whose map
// is pending or mapped (what Chrome reports as "[Buffer] used in submit while pending map").
(() => {
	const ev = { mapCalls: 0, mapOk: 0, mapFail: 0, failMsgs: [], submits: 0, badSubmits: 0, bad: [], raf: 0, mapLatency: [] };
	window.__gpuev = ev;
	const tick = () => { ev.raf++; requestAnimationFrame(tick); };
	requestAnimationFrame(tick);
	const st = new WeakMap();
	let ids = 0;
	const info = (b) => { let s = st.get(b); if (!s) { s = { id: ++ids, size: b.size, state: 'unmapped', maps: 0 }; st.set(b, s); } return s; };
	const P = GPUBuffer.prototype;
	const origMap = P.mapAsync, origUnmap = P.unmap;
	P.mapAsync = function (...a) {
		const s = info(this);
		s.state = 'pending'; s.maps++; ev.mapCalls++;
		const t0 = ev.raf;
		const p = origMap.apply(this, a);
		p.then(() => { if (s.state === 'pending') s.state = 'mapped'; ev.mapOk++; ev.mapLatency.push(ev.raf - t0); if (ev.mapLatency.length > 4000) ev.mapLatency.shift(); },
			(e) => { s.state = 'unmapped'; ev.mapFail++; if (ev.failMsgs.length < 20) ev.failMsgs.push(`buf#${s.id}: ${e && e.message}`); });
		return p;
	};
	P.unmap = function (...a) { info(this).state = 'unmapped'; return origUnmap.apply(this, a); };
	const encBufs = new WeakMap(), cbBufs = new WeakMap();
	const E = GPUCommandEncoder.prototype;
	const note = (enc, buf) => { let set = encBufs.get(enc); if (!set) { set = new Set(); encBufs.set(enc, set); } set.add(buf); };
	const oT2B = E.copyTextureToBuffer, oB2B = E.copyBufferToBuffer, oFinish = E.finish;
	E.copyTextureToBuffer = function (src, dst, ...r) { note(this, dst.buffer); return oT2B.call(this, src, dst, ...r); };
	E.copyBufferToBuffer = function (src, so, dst, ...r) { note(this, src); note(this, dst); return oB2B.call(this, src, so, dst, ...r); };
	E.finish = function (...a) { const cb = oFinish.apply(this, a); const set = encBufs.get(this); if (set) cbBufs.set(cb, set); return cb; };
	const Q = GPUQueue.prototype;
	const oSubmit = Q.submit;
	Q.submit = function (cbs) {
		ev.submits++;
		let bad = null;
		for (const cb of cbs) {
			const set = cbBufs.get(cb);
			if (!set) continue;
			for (const b of set) {
				const s = info(b);
				if (s.state !== 'unmapped') { bad = bad || []; bad.push(`buf#${s.id}(${s.size}) ${s.state} maps=${s.maps}`); }
			}
		}
		if (bad) {
			ev.badSubmits++;
			if (ev.bad.length < 10) ev.bad.push({ raf: ev.raf, bufs: bad.slice(0, 6), n: bad.length, stack: new Error().stack.split('\n').slice(2, 14).join(' | ') });
		}
		return oSubmit.call(this, cbs);
	};
})();
