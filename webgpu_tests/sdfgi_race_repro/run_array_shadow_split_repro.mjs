/**
 * Headless runner for array_shadow_split_repro.js (Task 9.5 Round 46b). See
 * array_shadow_split_index.html / array_shadow_split_repro.js for what this
 * tests.
 *
 * Usage:
 *   WEBGPU_REAL_GPU=1 node run_array_shadow_split_repro.mjs
 *   WEBGPU_REAL_GPU=1 node run_array_shadow_split_repro.mjs --history=4,30 --cycles=10
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
            const filePath = join(__dirname, urlPath === '/' ? 'array_shadow_split_index.html' : urlPath);
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
    const history = getArg('history', '4,30');
    const cycles = getArg('cycles', '10');

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

    const target = `${url}/?autorun&history=${history}&cycles=${cycles}`;
    console.log(`Navigating to ${target}`);
    await page.goto(target, { waitUntil: 'load' });

    console.log(`Running: history_sizes=${history}, cycles=${cycles} per history_size, buggy+fixed each...`);

    await page.waitForFunction(() => {
        const status = document.getElementById('status');
        return status && (status.className.includes('pass') || status.className.includes('fail'));
    }, undefined, { timeout: 300000 });

    const results = await page.evaluate(() => window.testResults);
    await browser.close();
    server.close();

    console.log('\n=== Results ===');
    if (results.error) {
        console.error('ERROR:', results.error);
        process.exit(1);
    }
    console.log('Adapter:', JSON.stringify(results.adapterInfo));
    for (const r of results.results) {
        console.log(`\n${r.buggyCopy ? 'BUGGY' : 'FIXED'} copy, history_size=${r.historySize}, calls=${r.calls}, converged=${r.converged}`);
        console.table(r.cycleSamples);
    }
    console.log(results.theoryConfirmed
        ? '\n>>> THEORY CONFIRMED: buggy copy extent diverges, fixed copy extent converges, for every history_size. <<<'
        : '\n>>> THEORY NOT CONFIRMED. <<<');

    process.exit(results.theoryConfirmed ? 0 : 2);
}

main().catch((err) => {
    console.error(err);
    process.exit(1);
});
