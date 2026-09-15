#!/usr/bin/env node
// Live-repro screenshot-trend capture: serves an exported WebGPU web build,
// loads it in headless Chromium with real-GPU flags, and takes a screenshot
// every `interval_ms` for `count` samples -- e.g. to chart a brightness/visual
// trend over time. See LIVE_REPRO_METHODOLOGY.md for the full workflow this
// is one half of (the other half is capture_console.mjs).
//
// Usage:
//   node capture_screenshots.mjs <build_dir> <out_prefix> <interval_ms> <count> [viewport_w] [viewport_h]
//
// Example (15 shots, 20s apart -> 300s total):
//   node capture_screenshots.mjs /tmp/scratch/my_project/builds /tmp/out/shot 20000 15
//
// Produces <out_prefix>_t<seconds>.png for each sample (e.g. shot_t20.png,
// shot_t40.png, ...) and prints every [error]-level console message plus a
// final ERROR COUNT summary -- pair with capture_console.mjs instead if you
// need every console line (including non-error / debug print_line output),
// not just screenshots.

import { chromium } from 'playwright';
import { createServer } from 'http';
import { readFileSync, existsSync, statSync } from 'fs';
import { join, extname } from 'path';

const DIR = process.argv[2];
const OUT_PREFIX = process.argv[3];
const INTERVAL_MS = parseInt(process.argv[4] || '20000', 10);
const COUNT = parseInt(process.argv[5] || '7', 10);
const VIEWPORT_W = parseInt(process.argv[6] || '2500', 10);
const VIEWPORT_H = parseInt(process.argv[7] || '1406', 10);

if (!DIR || !OUT_PREFIX) {
    console.error('Usage: node capture_screenshots.mjs <build_dir> <out_prefix> <interval_ms> <count> [viewport_w] [viewport_h]');
    process.exit(1);
}

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };

function startServer(dir) {
    return new Promise((resolve) => {
        const server = createServer((req, res) => {
            const url = req.url.split('?')[0];
            const filePath = join(dir, url === '/' ? 'index.html' : url);
            if (!existsSync(filePath) || statSync(filePath).isDirectory()) {
                res.writeHead(404);
                res.end('Not found');
                return;
            }
            const ext = extname(filePath);
            res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
            res.end(readFileSync(filePath));
        });
        server.listen(0, '127.0.0.1', () => resolve({ server, port: server.address().port }));
    });
}

const { server, port } = await startServer(DIR);

// See capture_console.mjs for why these specific flags matter -- forces real
// Vulkan-backed WebGPU (Dawn) instead of a silent SwiftShader/software fallback.
const browser = await chromium.launch({
    headless: true,
    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--use-angle=vulkan', '--use-vulkan=native', '--ignore-gpu-blocklist', '--no-sandbox'],
});
const page = await browser.newPage({ viewport: { width: VIEWPORT_W, height: VIEWPORT_H } });

const logs = [];
page.on('console', (msg) => logs.push(`[${msg.type()}] ${msg.text()}`));
page.on('pageerror', (err) => logs.push(`[pageerror] ${err.message}`));

await page.goto(`http://127.0.0.1:${port}/index.html`);

for (let i = 0; i < COUNT; i++) {
    await page.waitForTimeout(INTERVAL_MS);
    const path = `${OUT_PREFIX}_t${((i + 1) * INTERVAL_MS) / 1000}.png`;
    await page.screenshot({ path });
    console.log('captured', path);
}

const errorLines = logs.filter((l) => l.includes('[error]'));
console.log('ERROR COUNT:', errorLines.length);
for (const l of errorLines) console.log(l);

await browser.close();
server.close();
