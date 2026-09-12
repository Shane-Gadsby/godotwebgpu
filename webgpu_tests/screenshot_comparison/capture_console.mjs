#!/usr/bin/env node
// Live-repro console capture: serves an exported WebGPU web build, loads it in
// headless Chromium with real-GPU flags, and prints every console message for
// a fixed duration. See LIVE_REPRO_METHODOLOGY.md for the full workflow this
// is one half of (the other half is capture_screenshots.mjs).
//
// Usage:
//   node capture_console.mjs <build_dir> <duration_ms> [viewport_w] [viewport_h]
//
// Example:
//   node capture_console.mjs /tmp/scratch/my_project/builds 300000 > run.log
//
// Prints every console message as "[type] text" (all levels -- log/info/warn/
// error/pageerror -- not just errors, since this is also how engine-side
// print_line() debug instrumentation reaches you), live as it happens, and a
// final "[error] N" summary line when the duration elapses.

import { chromium } from 'playwright';
import { createServer } from 'http';
import { readFileSync, existsSync, statSync } from 'fs';
import { join, extname } from 'path';

const DIR = process.argv[2];
const DURATION_MS = parseInt(process.argv[3] || '60000', 10);
const VIEWPORT_W = parseInt(process.argv[4] || '2500', 10);
const VIEWPORT_H = parseInt(process.argv[5] || '1406', 10);

if (!DIR) {
    console.error('Usage: node capture_console.mjs <build_dir> <duration_ms> [viewport_w] [viewport_h]');
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
            // COOP/COEP required for SharedArrayBuffer (threaded/dlink web exports).
            res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
            res.end(readFileSync(filePath));
        });
        server.listen(0, '127.0.0.1', () => resolve({ server, port: server.address().port }));
    });
}

const { server, port } = await startServer(DIR);

// These flags force Chromium's WebGPU (Dawn) onto the machine's real Vulkan
// driver instead of falling back to SwiftShader/software rendering -- without
// them you will get a build that "runs" but tells you nothing about real GPU
// behavior. Always sanity-check the adapter is real (e.g. the engine's own
// startup log line naming the GPU) before trusting a capture.
const browser = await chromium.launch({
    headless: true,
    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--use-angle=vulkan', '--use-vulkan=native', '--ignore-gpu-blocklist', '--no-sandbox'],
});
const page = await browser.newPage({ viewport: { width: VIEWPORT_W, height: VIEWPORT_H } });

let errorCount = 0;
page.on('console', (msg) => {
    const line = `[${msg.type()}] ${msg.text()}`;
    console.log(line);
    if (msg.type() === 'error') errorCount++;
});
page.on('pageerror', (err) => {
    console.log(`[pageerror] ${err.message}`);
    errorCount++;
});

await page.goto(`http://127.0.0.1:${port}/index.html`);
await page.waitForTimeout(DURATION_MS);

console.log(`ERROR COUNT: ${errorCount}`);

await browser.close();
server.close();
