// Minimal CDP client over Node's global WebSocket, with flattened session support
// (needed to reach the Godot render-thread worker), plus a COOP/COEP static server.
import { createServer } from 'http';
import { readFileSync, existsSync, statSync } from 'fs';
import { join, extname } from 'path';

const MIME = {
	'.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
	'.pck': 'application/octet-stream', '.png': 'image/png', '.json': 'application/json',
	'.svg': 'image/svg+xml',
};

export function serve(dir, port) {
	const server = createServer((q, r) => {
		const p = decodeURIComponent(q.url.split('?')[0]);
		if (p === '/__blank') {
			r.writeHead(200, { 'Content-Type': 'text/html', 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' });
			r.end('<!doctype html><title>blank</title>');
			return;
		}
		const f = join(dir, p === '/' ? 'index.html' : p);
		if (!existsSync(f) || !statSync(f).isFile()) { r.writeHead(404); r.end(); return; }
		const body = readFileSync(f);
		r.writeHead(200, {
			'Content-Type': MIME[extname(f)] || 'application/octet-stream',
			'Content-Length': body.length,
			'Cross-Origin-Opener-Policy': 'same-origin',
			'Cross-Origin-Embedder-Policy': 'require-corp',
			'Cache-Control': 'no-store',
		});
		r.end(body);
	});
	return new Promise((res) => server.listen(port, '127.0.0.1', () => res(server)));
}

export class CDP {
	constructor(ws) { this.ws = ws; this.id = 0; this.pending = new Map(); this.handlers = new Map(); }

	static async attach(wsUrl) {
		const ws = new WebSocket(wsUrl);
		await new Promise((res, rej) => { ws.onopen = res; ws.onerror = (e) => rej(new Error('ws open failed')); });
		const c = new CDP(ws);
		ws.onmessage = (ev) => {
			const m = JSON.parse(ev.data);
			if (m.id !== undefined) {
				const p = c.pending.get(m.id); if (!p) return; c.pending.delete(m.id);
				m.error ? p.rej(new Error(m.error.message)) : p.res(m.result);
			} else {
				for (const h of c.handlers.get(m.method) || []) h(m.params, m.sessionId);
			}
		};
		return c;
	}

	send(method, params = {}, sessionId) {
		const id = ++this.id;
		const msg = { id, method, params };
		if (sessionId) msg.sessionId = sessionId;
		this.ws.send(JSON.stringify(msg));
		return new Promise((res, rej) => {
			this.pending.set(id, { res, rej });
			setTimeout(() => { if (this.pending.delete(id)) rej(new Error(method + ' timed out')); }, 120000);
		});
	}

	on(method, fn) {
		if (!this.handlers.has(method)) this.handlers.set(method, []);
		this.handlers.get(method).push(fn);
	}

	async eval(expr, awaitPromise = false, sessionId) {
		const r = await this.send('Runtime.evaluate', {
			expression: expr, returnByValue: true, awaitPromise,
		}, sessionId);
		if (r.exceptionDetails) throw new Error('eval threw: ' + (r.exceptionDetails.exception?.description || r.exceptionDetails.text));
		return r.result.value;
	}

	close() { try { this.ws.close(); } catch {} }
}

export async function targets(port = 9222) {
	const r = await fetch(`http://127.0.0.1:${port}/json/list`);
	return r.json();
}

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
