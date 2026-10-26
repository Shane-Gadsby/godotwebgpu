// usage: node probe.cjs <export dir> <out prefix> [chrome|firefox] [seconds]
// Serves the export with COOP/COEP, loads it headed on the real GPU with --verbose, and reports the adapter,
// shader stats, FSR 2 shader-cache lines and every error.
const pw = require('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/node_modules/playwright');
const http = require('http'), fs = require('fs'), path = require('path');
const [dir, out, which = 'chrome', secs = '20'] = process.argv.slice(2);
const types = { '.html': 'text/html', '.js': 'application/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png', '.bin': 'application/octet-stream' };
const server = http.createServer((q, r) => {
  let p = decodeURIComponent(q.url.split('?')[0]); if (p === '/') p = '/index.html';
  const f = path.join(dir, p);
  if (!f.startsWith(dir) || !fs.existsSync(f)) { r.writeHead(404); r.end(); return; }
  let body = fs.readFileSync(f);
  if (p === '/index.html') body = Buffer.from(body.toString().replace(/"args":\s*\[\]/, '"args":["--verbose"]'));
  r.writeHead(200, { 'Content-Type': types[path.extname(f)] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
  r.end(body);
});
(async () => {
  await new Promise((ok) => server.listen(0, '127.0.0.1', ok));
  const url = `http://127.0.0.1:${server.address().port}/`;
  let browser;
  if (which === 'firefox') {
    const { FIREFOX_WEBGPU_PREFS } = await import('/mnt/109313D2109313D2/godot-editors/godotwebgpu-27/webgpu_tests/scene_smoketest/firefox_prefs.mjs');
    browser = await pw.firefox.launch({ headless: false, firefoxUserPrefs: FIREFOX_WEBGPU_PREFS });
  } else {
    browser = await pw.chromium.launch({ headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'],
      args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'] });
  }
  const page = await (await browser.newContext({ viewport: { width: 1280, height: 720 } })).newPage();
  const lines = [];
  page.on('console', (m) => lines.push(`[${m.type()}] ${m.text()}`));
  page.on('pageerror', (e) => lines.push(`[pageerror] ${e.message}`));
  await page.goto(url);
  const adapter = await page.evaluate(async () => {
    const a = await navigator.gpu.requestAdapter();
    return a ? { vendor: a.info.vendor, arch: a.info.architecture, fallback: a.info.isFallbackAdapter, tex: a.limits.maxSampledTexturesPerShaderStage } : null;
  });
  await new Promise((r) => setTimeout(r, Number(secs) * 1000));
  const stats = await page.evaluate(() => window.godotWebGPUShaderStats || null);
  await page.screenshot({ path: `${out}.png` });
  fs.writeFileSync(`${out}.log`, lines.join('\n'));
  const pick = (re) => lines.filter((l) => re.test(l));
  console.log('adapter', JSON.stringify(adapter));
  console.log('stats', JSON.stringify(stats));
  console.log('renderer:', pick(/Forward\+|Forward Mobile|WebGPU .*Using/).slice(0, 3).join(' | '));
  const fsr = pick(/Fsr2/);
  console.log(`Fsr2 lines: ${fsr.length}; cache misses: ${pick(/cache miss for Fsr2/).length}`);
  for (const l of pick(/Fsr2.*(miss|SHA256|Mismatch)/).slice(0, 30)) console.log('  ', l.slice(0, 200));
  const errs = pick(/^\[(error|pageerror)\]/);
  console.log(`errors: ${errs.length}`);
  for (const l of errs.slice(0, 20)) console.log('  ', l.slice(0, 220));
  await browser.close(); server.close();
})().catch((e) => { console.error(e); process.exit(1); });
