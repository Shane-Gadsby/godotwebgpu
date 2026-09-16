/**
 * Headless runner for parallel_repro.js (Task 9.5 Round 45). See
 * parallel_index.html / parallel_repro.js for what this tests.
 *
 * This is heavier than prior rounds' repros: each cycle does a strided
 * readback of a 289x272 texture, and the final cycle of each config does a
 * full-grid readback (~314KB). Budget for a longer run than run_repro.mjs /
 * run_array_layer_repro.mjs — use run_in_background if driving this from an
 * agent loop, per plan-of-attack.md's own guidance for this round.
 *
 * Usage:
 *   WEBGPU_REAL_GPU=1 node run_parallel_repro.mjs
 *   WEBGPU_REAL_GPU=1 node run_parallel_repro.mjs --history=30 --filler=0,2,8 --cycles=3
 */

import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname } from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const MIME_TYPES = { '.html': 'text/html', '.js': 'text/javascript' };

function startServer(port = 0) {
    return new Promise((resolve) => {
        const server = createServer((req, res) => {
            const urlPath = new URL(req.url, 'http://localhost').pathname;
            const filePath = join(__dirname, urlPath === '/' ? 'parallel_index.html' : urlPath);
            if (!existsSync(filePath)) {
                res.writeHead(404);
                res.end('Not found');
                return;
            }
            const ext = extname(filePath);
            res.writeHead(200, { 'Content-Type': MIME_TYPES[ext] || 'application/octet-stream' });
            res.end(readFileSync(filePath));
        });
        server.listen(port, '127.0.0.1', () => {
            const addr = server.address();
            resolve({ server, url: `http://127.0.0.1:${addr.port}` });
        });
    });
}

async function launchChrome(pw) {
    const isCI = !!process.env.CI;
    const useRealGpu = !!process.env.WEBGPU_REAL_GPU;
    if (useRealGpu) {
        return pw.chromium.launch({
            headless: true,
            args: [
                '--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan',
                '--use-angle=vulkan', '--use-vulkan=native', '--ignore-gpu-blocklist', '--no-sandbox',
            ],
        });
    }
    if (isCI) {
        return pw.chromium.launch({
            headless: true,
            args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer', '--use-angle=swiftshader', '--enable-gpu'],
        });
    }
    return pw.chromium.launch({ headless: false, executablePath: '/usr/bin/google-chrome', args: [] });
}

async function main() {
    const args = process.argv.slice(2);
    const getArg = (name, def) => {
        const hit = args.find((a) => a.startsWith(`--${name}=`));
        return hit ? hit.split('=')[1] : def;
    };
    const history = getArg('history', '30');
    const filler = getArg('filler', '0,2,8');
    const cycles = getArg('cycles', '3');

    const { server, url } = await startServer();
    console.log(`Server running at ${url}`);

    let pw;
    try {
        pw = await import('playwright');
    } catch {
        console.error('Playwright not available.');
        server.close();
        process.exit(1);
    }

    console.log(`Launching Chrome (${process.env.WEBGPU_REAL_GPU ? 'real GPU' : 'default'})...`);
    const browser = await launchChrome(pw);
    const page = await browser.newPage();
    page.on('console', (msg) => {
        if (msg.type() === 'error') console.error(`  [browser] ${msg.text()}`);
    });
    page.on('pageerror', (err) => console.error(`  [pageerror] ${err}`));

    const target = `${url}/?autorun&history=${history}&filler=${filler}&cycles=${cycles}`;
    console.log(`Navigating to ${target}`);
    await page.goto(target, { waitUntil: 'load' });

    console.log(`Running: history_sizes=${history}, fillers=${filler}, cycles=${cycles} per config (289x272 grid, this can take several minutes)...`);

    await page.waitForFunction(() => {
        const status = document.getElementById('status');
        return status && (status.className.includes('pass') || status.className.includes('fail'));
    }, undefined, { timeout: 1200000 });

    const results = await page.evaluate(() => window.testResults);
    await browser.close();
    server.close();

    console.log('\n=== Results ===');
    if (results.error) {
        console.error('ERROR:', results.error);
        process.exit(1);
    }
    console.log('Adapter:', JSON.stringify(results.adapterInfo));
    console.log('Grid shape:', JSON.stringify(results.gridShape));
    for (const r of results.results) {
        console.log(`\nhistory_size=${r.historySize} filler=${r.filler} calls=${r.calls} diverged=${r.diverged} stillGrowing=${r.stillGrowing}`);
        console.table(r.cycleSamples.map(({ sampleMismatches, ...rest }) => rest));
        for (const s of r.cycleSamples) {
            if (s.sampleMismatches.length) {
                console.log(`  cycle ${s.cycle} sample mismatches:`, JSON.stringify(s.sampleMismatches));
            }
        }
    }
    console.log(results.reproduced
        ? '\n>>> REPRODUCED: at least one texel diverged from the expected plateau under concurrent dispatch. <<<'
        : '\n>>> NOT reproduced: every texel checked matched the expected plateau for every config tried. <<<');

    process.exit(results.reproduced ? 2 : 0);
}

main().catch((err) => {
    console.error(err);
    process.exit(1);
});
