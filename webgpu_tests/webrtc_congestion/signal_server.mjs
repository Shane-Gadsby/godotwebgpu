// Minimal WebSocket signaling server for the congestion tier — the one piece
// the parity tier deliberately skipped (webrtc_web.md §4.4: "it needs the thing
// this tier deliberately skipped: the Phase 1 signaling server").
//
// It relays opaque JSON between exactly two peers, an "offerer" and an
// "answerer", and tells both when the pair is complete so the offerer knows
// when it is worth making an offer. It understands nothing about SDP.
//
//   node signal_server.mjs            # ws://127.0.0.1:9099
//   SIGNAL_HOST=10.77.0.1 SIGNAL_PORT=9099 node signal_server.mjs
//
// No dependencies: RFC 6455 is short enough for text frames, and this tier runs
// inside a network namespace where `npm install` may not have network access.
// Only what Godot's WebSocketPeer actually sends is implemented — text frames,
// masked, with close and ping handled.
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';

const HOST = process.env.SIGNAL_HOST || '127.0.0.1';
const PORT = Number(process.env.SIGNAL_PORT || 9099);
const VERBOSE = process.env.SIGNAL_VERBOSE === '1';

const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const peers = new Map(); // role -> socket

function log(...a) {
	console.log('[signal]', ...a);
}

// ─── Frame encoding ───────────────────────────────────────────────────────────

function encodeText(str) {
	const payload = Buffer.from(str, 'utf-8');
	const len = payload.length;
	let header;
	if (len < 126) {
		header = Buffer.from([0x81, len]);
	} else if (len < 65536) {
		header = Buffer.alloc(4);
		header[0] = 0x81; header[1] = 126;
		header.writeUInt16BE(len, 2);
	} else {
		header = Buffer.alloc(10);
		header[0] = 0x81; header[1] = 127;
		header.writeBigUInt64BE(BigInt(len), 2);
	}
	return Buffer.concat([header, payload]);
}

// Decodes as many complete frames as `buf` holds. Returns the frames plus
// whatever bytes are left over, because a frame can straddle two TCP reads --
// which is exactly what happens once an SDP blob exceeds the MTU, and is the
// bug a naive "parse each chunk" server would only show under load.
function decodeFrames(buf) {
	const frames = [];
	let off = 0;
	for (;;) {
		if (buf.length - off < 2) break;
		const b0 = buf[off];
		const b1 = buf[off + 1];
		const opcode = b0 & 0x0f;
		const masked = (b1 & 0x80) !== 0;
		let len = b1 & 0x7f;
		let p = off + 2;
		if (len === 126) {
			if (buf.length - p < 2) break;
			len = buf.readUInt16BE(p); p += 2;
		} else if (len === 127) {
			if (buf.length - p < 8) break;
			len = Number(buf.readBigUInt64BE(p)); p += 8;
		}
		let mask = null;
		if (masked) {
			if (buf.length - p < 4) break;
			mask = buf.subarray(p, p + 4); p += 4;
		}
		if (buf.length - p < len) break;
		const payload = Buffer.from(buf.subarray(p, p + len));
		if (mask) {
			for (let i = 0; i < payload.length; i++) {
				payload[i] ^= mask[i & 3];
			}
		}
		frames.push({ opcode, payload });
		off = p + len;
	}
	return { frames, rest: buf.subarray(off) };
}

// ─── Server ───────────────────────────────────────────────────────────────────

const server = createServer((_req, res) => {
	res.writeHead(200, { 'Content-Type': 'text/plain' });
	res.end('signaling: websocket only\n');
});

server.on('upgrade', (req, socket) => {
	const key = req.headers['sec-websocket-key'];
	if (!key) {
		socket.destroy();
		return;
	}
	socket.write(
		'HTTP/1.1 101 Switching Protocols\r\n'
		+ 'Upgrade: websocket\r\n'
		+ 'Connection: Upgrade\r\n'
		+ `Sec-WebSocket-Accept: ${createHash('sha1').update(key + GUID).digest('base64')}\r\n`
		+ '\r\n');
	socket.setNoDelay(true);

	let role = null;
	let buf = Buffer.alloc(0);

	const send = (obj) => {
		try {
			socket.write(encodeText(JSON.stringify(obj)));
		} catch { /* peer went away */ }
	};

	socket.on('data', (chunk) => {
		buf = Buffer.concat([buf, chunk]);
		const { frames, rest } = decodeFrames(buf);
		buf = rest;
		for (const { opcode, payload } of frames) {
			if (opcode === 0x8) { // close
				socket.end();
				return;
			}
			if (opcode === 0x9) { // ping -> pong
				socket.write(Buffer.concat([Buffer.from([0x8a, payload.length]), payload]));
				continue;
			}
			if (opcode !== 0x1) {
				continue;
			}
			let msg;
			try {
				msg = JSON.parse(payload.toString('utf-8'));
			} catch {
				continue;
			}
			if (msg.type === 'hello') {
				role = msg.role === 'answerer' ? 'answerer' : 'offerer';
				if (peers.has(role)) {
					// Two peers claiming the same role means a stale process is
					// still attached. Say so: silently replacing it produces a
					// run that connects to the wrong peer and is very hard to
					// read back from the logs.
					log(`WARNING replacing an existing "${role}" -- stale process?`);
					try { peers.get(role).end(); } catch { /* already gone */ }
				}
				peers.set(role, socket);
				log(`${role} joined (${peers.size}/2)`);
				if (peers.has('offerer') && peers.has('answerer')) {
					log('pair complete, releasing both');
					for (const s of peers.values()) {
						try { s.write(encodeText(JSON.stringify({ type: 'ready' }))); } catch { /* gone */ }
					}
				}
				continue;
			}
			// Everything else is relayed verbatim to the other peer.
			const other = role === 'offerer' ? peers.get('answerer') : peers.get('offerer');
			if (VERBOSE) {
				log(`relay ${role} -> ${role === 'offerer' ? 'answerer' : 'offerer'}: ${msg.type}`);
			}
			if (other) {
				try { other.write(encodeText(JSON.stringify(msg))); } catch { /* gone */ }
			} else if (msg.type !== 'bye') {
				log(`dropped "${msg.type}" from ${role}: no peer attached`);
			}
		}
	});

	const gone = () => {
		if (role && peers.get(role) === socket) {
			peers.delete(role);
			log(`${role} left (${peers.size}/2)`);
		}
	};
	socket.on('close', gone);
	socket.on('error', gone);
});

server.listen(PORT, HOST, () => {
	log(`listening on ws://${HOST}:${PORT}`);
});

for (const sig of ['SIGINT', 'SIGTERM']) {
	process.on(sig, () => {
		server.close();
		process.exit(0);
	});
}
