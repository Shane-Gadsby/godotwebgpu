// attachAudit(page, tag): with WEBGPU_AUDIT=<file.jsonl> in the environment, inject webgpu_audit.js into the page and
// append every "[audit]" record it logs to that file as one JSON line ({ tag, ...record }). Without it, a no-op.
import { appendFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const SCRIPT = join(dirname(fileURLToPath(import.meta.url)), 'webgpu_audit.js');

export async function attachAudit(page, tag) {
	const out = process.env.WEBGPU_AUDIT;
	if (!out) {
		return;
	}
	await page.addInitScript({ path: SCRIPT });
	page.on('console', (m) => {
		const t = m.text();
		if (t.startsWith('[audit] ')) {
			appendFileSync(out, JSON.stringify({ tag, ...JSON.parse(t.slice(8)) }) + '\n');
		}
	});
}
