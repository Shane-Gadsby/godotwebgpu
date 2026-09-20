#!/usr/bin/env node
// Drives an exported WebGPU web build in headless Chromium (real GPU) with
// window.GODOT_WEBGPU_RECORD_SPEC_CONSTANTS enabled from page load, then pulls
// window.__godotWebGPUSpecConstantRecording directly (skipping the manual
// devtools-console download flow described in
// drivers/webgpu/rendering_device_driver_webgpu.cpp's _record_spec_constant_usage()
// doc comment, since that flow is for a human at devtools, not an automated
// capture) and writes it to a JSON file, with a console summary.
//
// This is Task 13's Phase 1 (recording) exercised end to end, per
// webgpu_notes/TASKS.md's 2026-09-20 scoping update -- used here to get real
// numbers on how many distinct (shader, spec-constant-value) combinations a
// project's initial scene actually produces, before sizing Phases 2/3 (the
// export-time baking + storage-format work).
//
// Usage:
//   node capture_spec_constant_recording.mjs <build_dir> <duration_ms> <out_file.json> [viewport_w] [viewport_h]

import { chromium } from 'playwright';
import { createServer } from 'http';
import { readFileSync, writeFileSync, existsSync, statSync } from 'fs';
import { join, extname } from 'path';

const DIR = process.argv[2];
const DURATION_MS = parseInt(process.argv[3] || '30000', 10);
const OUT_FILE = process.argv[4];
const VIEWPORT_W = parseInt(process.argv[5] || '2500', 10);
const VIEWPORT_H = parseInt(process.argv[6] || '1406', 10);

if (!DIR || !OUT_FILE) {
    console.error('Usage: node capture_spec_constant_recording.mjs <build_dir> <duration_ms> <out_file.json> [viewport_w] [viewport_h]');
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

const browser = await chromium.launch({
    headless: true,
    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--use-angle=vulkan', '--use-vulkan=native', '--ignore-gpu-blocklist', '--no-sandbox'],
});
const page = await browser.newPage({ viewport: { width: VIEWPORT_W, height: VIEWPORT_H } });

// Must run before any page script (including the engine's own bootstrap), so
// the flag is already set by the time _record_spec_constant_usage()'s first
// EM_ASM_INT check runs.
await page.addInitScript(() => {
    window.GODOT_WEBGPU_RECORD_SPEC_CONSTANTS = true;
});

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

const recording = await page.evaluate(() => window.__godotWebGPUSpecConstantRecording || []);

writeFileSync(OUT_FILE, JSON.stringify(recording, null, 2));

const byShader = new Map();
for (const entry of recording) {
    byShader.set(entry.base_spv_hash, (byShader.get(entry.base_spv_hash) || 0) + 1);
}

console.log(`ERROR COUNT: ${errorCount}`);
console.log(`RECORDED ENTRIES: ${recording.length}`);
console.log(`DISTINCT BASE SHADERS: ${byShader.size}`);
for (const [hash, count] of byShader.entries()) {
    console.log(`  base_spv_hash=${hash}: ${count} distinct spec-constant combo(s)`);
}
console.log(`Wrote ${OUT_FILE}`);

await browser.close();
server.close();
