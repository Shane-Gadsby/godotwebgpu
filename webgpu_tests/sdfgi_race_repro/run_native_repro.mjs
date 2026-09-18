/**
 * Headless runner for native_emdawnwebgpu_repro.cpp — the same race repro
 * as run_repro.mjs/race_repro.js, but compiled through the actual
 * Emscripten + emdawnwebgpu C API path the real engine uses
 * (--use-port=emdawnwebgpu), not raw browser JS. Build first:
 *
 *   source ~/emsdk/emsdk_env.sh
 *   em++ -O2 --use-port=emdawnwebgpu -sEXIT_RUNTIME=0 -sASSERTIONS=1 -std=c++17 \
 *     native_emdawnwebgpu_repro.cpp -o native_repro.html
 *
 * Usage:
 *   WEBGPU_REAL_GPU=1 node run_native_repro.mjs
 *   WEBGPU_REAL_GPU=1 node run_native_repro.mjs --calls=2000 --slack=0,1,2,4,8,16,32,64
 */

import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname } from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const MIME_TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };

function startServer(port = 0) {
    return new Promise((resolve) => {
        const server = createServer((req, res) => {
            const urlPath = new URL(req.url, 'http://localhost').pathname;
            const filePath = join(__dirname, urlPath === '/' ? 'native_repro.html' : urlPath);
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
    return pw.chromium.launch({ headless: false, executablePath: '/usr/bin/google-chrome-stable', args: ['--use-vulkan', '--enable-features=Vulkan', '--ignore-gpu-blocklist'] });
}

async function main() {
    const args = process.argv.slice(2);
    const getArg = (name, def) => {
        const hit = args.find((a) => a.startsWith(`--${name}=`));
        return hit ? hit.split('=')[1] : def;
    };
    const calls = getArg('calls', '500');
    const slack = getArg('slack', '0,1,2,4,8,16,32');

    if (!existsSync(join(__dirname, 'native_repro.html'))) {
        console.error('native_repro.html not found — build it first, see file header comment.');
        process.exit(1);
    }

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

    const lines = [];
    let done = false;
    let summary = null;
    page.on('console', (msg) => {
        const text = msg.text();
        lines.push(text);
        if (text.startsWith('RESULT_SUMMARY')) {
            summary = text;
            done = true;
        }
        if (text.startsWith('RESULT_ERROR')) {
            console.error('  [browser]', text);
            done = true;
        }
    });
    page.on('pageerror', (err) => console.error(`  [pageerror] ${err}`));

    // The compiled program reads argv from Module.arguments; simplest is to
    // pass calls/slack via URL query and read them in a small JS shim, but
    // since the .cpp reads argv directly, we instead just rebuild-free reuse
    // defaults unless the caller edited argv in native_repro.js. To keep this
    // runner build-free, we pass calls/slack via a global the .html sets from
    // location.search before Module starts, and the .cpp's argv parsing
    // reads Module.arguments (see native_repro.html injection below).
    await page.goto(`${url}/?calls=${calls}&slack=${slack}`, { waitUntil: 'load' });

    console.log(`Running native emdawnwebgpu repro: calls-per-slack=${calls}, slack=${slack} (may take a while)...`);

    const start = Date.now();
    while (!done && Date.now() - start < 300000) {
        await new Promise((r) => setTimeout(r, 500));
    }

    await browser.close();
    server.close();

    console.log('\n=== Browser console ===');
    for (const l of lines) console.log('  ' + l);

    if (!summary) {
        console.error('\nTimed out or errored before completion.');
        process.exit(1);
    }
    const reproduced = summary.includes('reproduced=1');
    console.log(reproduced
        ? '\n>>> REPRODUCED via the real emdawnwebgpu C API path. <<<'
        : '\n>>> NOT reproduced via the real emdawnwebgpu C API path either. <<<');
    process.exit(reproduced ? 2 : 0);
}

main().catch((err) => {
    console.error(err);
    process.exit(1);
});
