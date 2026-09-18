/**
 * Headless runner for the SDFGI same-texel race repro (Tier 1 Phase 1, step 1
 * of plan-of-attack.md's investigation plan).
 *
 * Usage:
 *   node run_repro.mjs                          (swiftshader, sandbox default)
 *   WEBGPU_REAL_GPU=1 node run_repro.mjs         (real Vulkan-backed GPU — the
 *                                                  meaningful run; swiftshader
 *                                                  is a materially different
 *                                                  adapter, see webgpu_notes/
 *                                                  TASKS.md Task 9.5)
 *   node run_repro.mjs --calls=2000 --slack=0,1,2,4,8,16,32,64
 *   node run_repro.mjs --serve-only
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
            const filePath = join(__dirname, urlPath === '/' ? 'index.html' : urlPath);
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
                '--enable-unsafe-webgpu',
                '--enable-features=Vulkan',
                '--use-vulkan',
                '--use-angle=vulkan',
                '--use-vulkan=native',
                '--ignore-gpu-blocklist',
                '--no-sandbox',
            ],
        });
    }
    if (isCI) {
        return pw.chromium.launch({
            headless: true,
            args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan,UseSkiaRenderer', '--use-angle=swiftshader', '--enable-gpu'],
        });
    }
    const executablePath = process.platform === 'darwin'
        ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
        : process.platform === 'win32'
            ? 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe'
            : '/usr/bin/google-chrome-stable';
    return pw.chromium.launch({ headless: false, executablePath, args: ['--use-vulkan', '--enable-features=Vulkan', '--ignore-gpu-blocklist'] });
}

async function main() {
    const args = process.argv.slice(2);
    const getArg = (name, def) => {
        const hit = args.find((a) => a.startsWith(`--${name}=`));
        return hit ? hit.split('=')[1] : def;
    };
    const calls = getArg('calls', '500');
    const slack = getArg('slack', '0,1,2,4,8,16,32');

    const { server, url } = await startServer();
    console.log(`Server running at ${url}`);

    if (args.includes('--serve-only')) {
        console.log('Serving files. Open in a WebGPU-capable browser manually. Press Ctrl+C to stop.');
        return;
    }

    let pw;
    try {
        pw = await import('playwright');
    } catch {
        console.error('Playwright not available. npm install playwright && npx playwright install chromium');
        server.close();
        process.exit(1);
    }

    console.log(`Launching Chrome (${process.env.WEBGPU_REAL_GPU ? 'real GPU' : process.env.CI ? 'CI/swiftshader' : 'system Chrome'})...`);
    const browser = await launchChrome(pw);
    const page = await browser.newPage();
    page.on('console', (msg) => {
        if (msg.type() === 'error') console.error(`  [browser] ${msg.text()}`);
    });
    page.on('pageerror', (err) => console.error(`  [pageerror] ${err}`));

    const target = `${url}/?autorun&calls=${calls}&slack=${slack}`;
    console.log(`Navigating to ${target}`);
    await page.goto(target, { waitUntil: 'load' });

    console.log(`Running: calls-per-slack=${calls}, slack values=${slack} (this submits ~${Number(calls) * (1 + slack.split(',').reduce((a, b) => a + Number(b), 0) / slack.split(',').length)} command buffers total, may take a while)...`);

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
    console.table(results.results);
    console.log(results.reproduced
        ? '\n>>> REPRODUCED: hazard-tracking divergence detected in a from-scratch, Godot-free WebGPU program. <<<'
        : '\n>>> NOT reproduced: no divergence at any slack value tried. <<<');

    process.exit(results.reproduced ? 2 : 0);
}

main().catch((err) => {
    console.error(err);
    process.exit(1);
});
