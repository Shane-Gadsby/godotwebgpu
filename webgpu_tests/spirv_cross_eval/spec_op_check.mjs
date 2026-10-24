// Runs the modules spec_op_probes.py generated (converted by `tint_convert_cli --raw`) in Chrome: builds a compute
// pipeline with the shader's default spec constants and again with the alternate ones passed as pipeline constants,
// and compares the u32 each writes to o.v[0] with manifest.json. Prints one line per module and a summary; exit 1 on
// any mismatch that is not listed in $EXPECT_FAIL (comma separated names).
//
// LEGACY=1 checks the frozen-module path instead: <wgsl_dir>/<name>.alt.wgsl is a module whose spec-constant defaults
// were patched to the alternate values and converted through the full run_pipeline (what
// _create_module_with_spec_constants() does); it is built with no pipeline constants and must give expectAlt.
//
// Usage: node spec_op_check.mjs <probes_dir> <wgsl_dir>
import { createRequire } from 'module';
import fs from 'fs';
import http from 'http';
import path from 'path';
import { fileURLToPath } from 'url';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.join(here, '..', 'scene_smoketest', 'package.json'));
const { chromium } = require('playwright');
const [probes, wgslDir] = process.argv.slice(2);
if (!probes || !wgslDir) {
	console.error('usage: node spec_op_check.mjs <probes_dir> <wgsl_dir>');
	process.exit(2);
}
const manifest = JSON.parse(fs.readFileSync(path.join(probes, 'manifest.json'), 'utf8'));
const consts = manifest._consts;
const legacy = process.env.LEGACY === '1';
const expectFail = new Set((process.env.EXPECT_FAIL || '').split(',').filter(Boolean));

const server = http.createServer((req, res) => {
	res.setHeader('content-type', 'text/html');
	res.end('<html><body>spec op check</body></html>');
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const browser = await chromium.launch({
	headless: false,
	executablePath: '/usr/bin/google-chrome-stable',
	args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
	ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
});
const page = await browser.newPage();
await page.goto(`http://127.0.0.1:${server.address().port}/`);

const run = (code, constants) =>
	page.evaluate(
		async ({ code, constants }) => {
			const adapter = await navigator.gpu.requestAdapter();
			const device = await adapter.requestDevice();
			device.pushErrorScope('validation');
			const module = device.createShaderModule({ code });
			const buf = device.createBuffer({ size: 16, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
			const rb = device.createBuffer({ size: 16, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
			let pipeline;
			try {
				pipeline = await device.createComputePipelineAsync({ layout: 'auto', compute: { module, entryPoint: 'main', constants } });
			} catch (e) {
				await device.popErrorScope();
				return { error: String(e.message).split('\n')[0] };
			}
			const bg = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: buf } }] });
			const enc = device.createCommandEncoder();
			const pass = enc.beginComputePass();
			pass.setPipeline(pipeline);
			pass.setBindGroup(0, bg);
			pass.dispatchWorkgroups(1);
			pass.end();
			enc.copyBufferToBuffer(buf, 0, rb, 0, 16);
			device.queue.submit([enc.finish()]);
			await rb.mapAsync(GPUMapMode.READ);
			const v = new Uint32Array(rb.getMappedRange().slice(0))[0];
			await device.popErrorScope();
			device.destroy();
			return { value: v };
		},
		{ code, constants },
	);

let pass = 0,
	bad = 0,
	unexpected = 0;
const rows = [];
for (const name of Object.keys(manifest).filter((n) => !n.startsWith('_'))) {
	const f = path.join(wgslDir, name + (legacy ? '.alt.wgsl' : '.wgsl'));
	if (!fs.existsSync(f) || fs.statSync(f).size === 0) {
		rows.push([name, manifest[name].category, 'NOCONV', '']);
		bad++;
		if (!expectFail.has(name)) unexpected++;
		continue;
	}
	const code = fs.readFileSync(f, 'utf8');
	const ids = new Set([...code.matchAll(/@id\((\d+)\)/g)].map((m) => m[1]));
	const pick = (set) => Object.fromEntries(Object.entries(set).filter(([k]) => ids.has(k)));
	const d = legacy ? await run(code, {}) : await run(code, pick(consts.default));
	const a = legacy ? d : await run(code, pick(consts.alt));
	let verdict = 'PASS';
	let detail = '';
	if (d.error || a.error) {
		verdict = 'FAIL';
		detail = d.error || a.error;
	} else {
		const wantD = (legacy ? manifest[name].expectAlt : manifest[name].expectDefault) >>> 0;
		const wantA = manifest[name].expectAlt >>> 0;
		if (d.value !== wantD || a.value !== wantA) {
			verdict = 'WRONG';
			detail = `default got ${d.value} want ${wantD}; alt got ${a.value} want ${wantA}; ids ${[...ids]}`;
		}
	}
	rows.push([name, manifest[name].category, verdict, detail]);
	if (verdict === 'PASS') {
		pass++;
		if (expectFail.has(name)) {
			unexpected++;
			rows[rows.length - 1][3] = 'listed in EXPECT_FAIL but passes now: remove it';
		}
	}
	else {
		bad++;
		if (!expectFail.has(name)) unexpected++;
	}
}
for (const r of rows) console.log(`${r[2].padEnd(7)} ${r[0].padEnd(32)} ${r[1].padEnd(28)} ${r[3]}`);
console.log(`spec_op_check: ${pass} pass, ${bad} not passing (${unexpected} unexpected)`);
await browser.close();
server.close();
process.exit(unexpected ? 1 : 0);
