// Asserts the real-GPU Chrome adapter with the required launch flags (lead's rule); exit 1 otherwise.
import { chromium } from '/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/forward_plus/node_modules/playwright/index.mjs';
const b = await chromium.launch({ executablePath: '/usr/bin/google-chrome-stable', headless: false,
  args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--ignore-gpu-blocklist'],
  ignoreDefaultArgs: ['--enable-unsafe-swiftshader'] });
const p = await b.newPage();
await p.goto('file:///tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w49/blank.html');
const r = await p.evaluate(async () => { const a = await navigator.gpu.requestAdapter(); return a ? { v: a.info.vendor, arch: a.info.architecture, fb: a.info.isFallbackAdapter, t: a.limits.maxSampledTexturesPerShaderStage } : null; });
await b.close();
console.log('adapter', JSON.stringify(r));
if (!r || r.v !== 'nvidia' || r.arch !== 'lovelace' || r.fb || r.t < 48) { console.log('ADAPTER BAD'); process.exit(1); }
console.log('ADAPTER OK');
