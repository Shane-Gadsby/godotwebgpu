import { chromium } from 'playwright';
import { createServer } from 'http';
import { readFileSync, existsSync, statSync } from 'fs';
import { join, extname } from 'path';

const DIR = process.argv[2];
const DURATION_MS = parseInt(process.argv[3] || '20000', 10);
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.pck': 'application/octet-stream', '.png': 'image/png' };
function startServer(dir) {
    return new Promise((resolve) => {
        const server = createServer((req, res) => {
            const url = req.url.split('?')[0];
            const filePath = join(dir, url === '/' ? 'index.html' : url);
            if (!existsSync(filePath) || statSync(filePath).isDirectory()) { res.writeHead(404); res.end('Not found'); return; }
            const ext = extname(filePath);
            res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
            res.end(readFileSync(filePath));
        });
        server.listen(0, '127.0.0.1', () => resolve({ server, port: server.address().port }));
    });
}
const { server, port } = await startServer(DIR);
const browser = await chromium.launch({ headless: true, args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan', '--use-angle=vulkan', '--use-vulkan=native', '--ignore-gpu-blocklist', '--no-sandbox'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
await page.addInitScript(() => {
    window.ENV = window.ENV || {};
    window.ENV.ASAN_OPTIONS = 'symbolize=0:print_stats=0';
});
page.on('console', (msg) => console.log(`[${msg.type()}] ${msg.text()}`));
page.on('pageerror', (err) => console.log(`[pageerror] ${err.message}`));
await page.goto(`http://127.0.0.1:${port}/index.html`);
await page.waitForTimeout(DURATION_MS);
await browser.close();
server.close();
