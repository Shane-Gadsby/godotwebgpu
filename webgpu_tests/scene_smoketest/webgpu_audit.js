// WebGPU shader/pipeline audit, injected as a Playwright init script (page.addInitScript({ path })) when a runner is
// started with WEBGPU_AUDIT=<file.jsonl> (run_scenes.mjs, forward_plus/run_forward_plus.mjs, perf_guard/frame_time.mjs).
//
// Why: Firefox compiles WGSL with naga, not Tint, and its console reports "1 error(s)" for a rejected module without
// the message; getCompilationInfo() is the only way to read it (memory "Firefox WebGPU blockers"). This asks every
// module for its compilation info and wraps every pipeline creation in a validation error scope, so each failure is
// tied to the module or pipeline label that caused it. It reports through the console with an "[audit]" prefix
// (the runner appends those lines to the jsonl file): failures with console.error, so a runner that counts console
// errors still counts them although the error scope keeps them away from the device's uncapturederror listener.
// Every module and pipeline also gets an "ok" record, so a run that audited nothing is visible as zero records.
(() => {
	if (typeof GPUDevice === 'undefined') {
		return;
	}
	const send = (rec, bad) => {
		const line = '[audit] ' + JSON.stringify(rec);
		(bad ? console.error : console.log).call(console, line);
	};
	const D = GPUDevice.prototype;
	let modules = 0;
	const createShaderModule = D.createShaderModule;
	D.createShaderModule = function (desc) {
		const m = createShaderModule.call(this, desc);
		const id = ++modules;
		const label = (desc && desc.label) || '';
		const bytes = desc && typeof desc.code === 'string' ? desc.code.length : 0;
		if (m && m.getCompilationInfo) {
			m.getCompilationInfo().then((info) => {
				const errs = info.messages.filter((x) => x.type === 'error');
				const warns = info.messages.filter((x) => x.type === 'warning');
				const fmt = (x) => `${x.lineNum}:${x.linePos} ${x.message}`.slice(0, 1500);
				if (errs.length) {
					// The first 400 lines locate the error without shipping a 300 KB uber shader into the log.
					send({ kind: 'module', ok: false, id, label, bytes, errors: errs.map(fmt), code: desc.code.split('\n').slice(0, 400).join('\n') }, true);
				} else {
					send({ kind: 'module', ok: true, id, label, bytes, warnings: warns.map(fmt).slice(0, 3) });
				}
			}, (e) => send({ kind: 'module', ok: false, id, label, bytes, errors: ['getCompilationInfo rejected: ' + e] }, true));
		}
		return m;
	};
	const stageLabels = (desc) => [desc.vertex, desc.fragment, desc.compute].filter(Boolean).map((s) => (s.module && s.module.label) || '?');
	for (const name of ['createRenderPipeline', 'createComputePipeline']) {
		const orig = D[name];
		D[name] = function (desc) {
			this.pushErrorScope('validation');
			const p = orig.call(this, desc);
			this.popErrorScope().then((e) => {
				const rec = { kind: 'pipeline', fn: name, label: (desc && desc.label) || '', modules: stageLabels(desc || {}) };
				if (e) {
					send(Object.assign(rec, { ok: false, error: String(e.message).slice(0, 3000) }), true);
				} else {
					send(Object.assign(rec, { ok: true }));
				}
			});
			return p;
		};
	}
	for (const name of ['createRenderPipelineAsync', 'createComputePipelineAsync']) {
		const orig = D[name];
		if (!orig) {
			continue;
		}
		D[name] = function (desc) {
			const rec = { kind: 'pipeline', fn: name, label: (desc && desc.label) || '', modules: stageLabels(desc || {}) };
			return orig.call(this, desc).then((p) => {
				send(Object.assign(rec, { ok: true }));
				return p;
			}, (e) => {
				send(Object.assign(rec, { ok: false, error: String((e && e.message) || e).slice(0, 3000) }), true);
				throw e;
			});
		};
	}
})();
