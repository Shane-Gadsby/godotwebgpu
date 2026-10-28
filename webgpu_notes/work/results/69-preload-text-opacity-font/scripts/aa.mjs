// Renders page.html (CSS variants of the download line) and writes a screenshot per browser. usage: node aa.mjs <chrome|firefox> <out.png>
import { createServer } from 'http';
import { readFile } from 'fs/promises';
import { createRequire } from 'module';
const require = createRequire('/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/screenshot_comparison/package.json');
const { chromium, firefox } = require('playwright');
const { FIREFOX_WEBGPU_PREFS } = await import('/mnt/109313D2109313D2/godot-editors/godotwebgpu-69/webgpu_tests/scene_smoketest/firefox_prefs.mjs');
const [B, OUT] = process.argv.slice(2);
const html = await readFile(new URL('./aa_page.html', import.meta.url));
const s = createServer((q, r) => { r.writeHead(200, { 'Content-Type': 'text/html' }); r.end(html); }).listen(8703, '127.0.0.1');
const browser = B === 'firefox' ? await firefox.launch({ headless: false, firefoxUserPrefs: FIREFOX_WEBGPU_PREFS })
	: await chromium.launch({ headless: false, executablePath: '/usr/bin/google-chrome-stable', ignoreDefaultArgs: ['--enable-unsafe-swiftshader'], args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'] });
const page = await (await browser.newContext({ viewport: { width: 1100, height: 800 } })).newPage();
await page.goto('http://127.0.0.1:8703/');
const a = await page.evaluate(async () => { const x = navigator.gpu && await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' }); return x ? { v: x.info.vendor, a: x.info.architecture, f: !!x.info.isFallbackAdapter, t: x.limits.maxSampledTexturesPerShaderStage } : null; });
console.log('adapter', JSON.stringify(a));
if (B === 'firefox' ? !a : (!a || !/nvidia/.test(a.v) || !/lovelace/.test(a.a) || a.f || a.t !== 48)) { console.log('FAIL adapter'); process.exit(3); }
await page.waitForTimeout(500);
await page.screenshot({ path: OUT });
await browser.close(); s.close();
