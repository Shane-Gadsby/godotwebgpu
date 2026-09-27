/**
 * Minimal PNG decode/encode, using only node's built-in zlib.
 *
 * Exists because comparing screenshots by their *compressed* bytes is
 * meaningless: deflate is not locally stable, so a single changed pixel
 * rewrites most of the stream and reads as a ~99% "difference" (which is
 * exactly what the screenshot job reported for images that are visually
 * identical). Everything here decodes to straight RGBA8 so the comparison can
 * be per-pixel with a real tolerance.
 *
 * Covers what Playwright's `page.screenshot({ type: 'png' })` actually emits:
 * non-interlaced, bit depth 8 or 16, color types 0/2/4/6. Anything else
 * throws rather than guessing.
 */

import { inflateSync, deflateSync } from 'zlib';

const PNG_SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

const CHANNELS = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 };

/** Decode a PNG buffer to { width, height, data } with `data` as RGBA8. */
export function decodePng(buffer) {
	if (buffer.length < 8 || !buffer.subarray(0, 8).equals(PNG_SIGNATURE)) {
		throw new Error('not a PNG (bad signature)');
	}

	let width = 0, height = 0, bitDepth = 0, colorType = 0, interlace = 0;
	let palette = null;
	let transparency = null;
	const idat = [];

	let offset = 8;
	while (offset + 8 <= buffer.length) {
		const length = buffer.readUInt32BE(offset);
		const type = buffer.toString('ascii', offset + 4, offset + 8);
		const body = buffer.subarray(offset + 8, offset + 8 + length);
		offset += 12 + length; // length + type + data + CRC

		if (type === 'IHDR') {
			width = body.readUInt32BE(0);
			height = body.readUInt32BE(4);
			bitDepth = body[8];
			colorType = body[9];
			interlace = body[12];
		} else if (type === 'PLTE') {
			palette = Buffer.from(body);
		} else if (type === 'tRNS') {
			transparency = Buffer.from(body);
		} else if (type === 'IDAT') {
			idat.push(body);
		} else if (type === 'IEND') {
			break;
		}
	}

	if (!width || !height) throw new Error('PNG has no IHDR');
	if (interlace !== 0) throw new Error('interlaced PNG is not supported');
	if (bitDepth !== 8 && bitDepth !== 16) throw new Error(`unsupported PNG bit depth ${bitDepth}`);
	const channels = CHANNELS[colorType];
	if (channels === undefined) throw new Error(`unsupported PNG color type ${colorType}`);
	if (colorType === 3 && !palette) throw new Error('indexed PNG with no PLTE');

	const bytesPerSample = bitDepth / 8;
	const bpp = channels * bytesPerSample; // bytes per pixel, for the filters
	const stride = width * bpp;
	const raw = unfilter(inflateSync(Buffer.concat(idat)), width, height, bpp, stride);

	// Expand whatever came out to RGBA8.
	const out = Buffer.alloc(width * height * 4);
	for (let i = 0, p = 0; i < width * height; i++, p += 4) {
		const s = i * bpp;
		// For 16-bit samples the high byte is a good enough 8-bit value here:
		// this is a visual comparison, not a color-managed conversion.
		const sample = (c) => raw[s + c * bytesPerSample];
		let r, g, b, a = 255;
		switch (colorType) {
			case 0: r = g = b = sample(0); break;
			case 2: r = sample(0); g = sample(1); b = sample(2); break;
			case 3: {
				const idx = raw[s];
				r = palette[idx * 3]; g = palette[idx * 3 + 1]; b = palette[idx * 3 + 2];
				if (transparency && idx < transparency.length) a = transparency[idx];
				break;
			}
			case 4: r = g = b = sample(0); a = sample(1); break;
			case 6: r = sample(0); g = sample(1); b = sample(2); a = sample(3); break;
		}
		out[p] = r; out[p + 1] = g; out[p + 2] = b; out[p + 3] = a;
	}

	return { width, height, data: out };
}

/** Reverse the per-scanline PNG filters, dropping the filter-type byte. */
function unfilter(inflated, width, height, bpp, stride) {
	const out = Buffer.alloc(height * stride);
	let src = 0;
	for (let y = 0; y < height; y++) {
		const filter = inflated[src++];
		const row = y * stride;
		const prev = row - stride;
		for (let x = 0; x < stride; x++) {
			const rawByte = inflated[src + x];
			const a = x >= bpp ? out[row + x - bpp] : 0;
			const b = y > 0 ? out[prev + x] : 0;
			const c = x >= bpp && y > 0 ? out[prev + x - bpp] : 0;
			let value;
			switch (filter) {
				case 0: value = rawByte; break;
				case 1: value = rawByte + a; break;
				case 2: value = rawByte + b; break;
				case 3: value = rawByte + ((a + b) >> 1); break;
				case 4: value = rawByte + paeth(a, b, c); break;
				default: throw new Error(`unknown PNG filter type ${filter} on row ${y}`);
			}
			out[row + x] = value & 0xff;
		}
		src += stride;
	}
	return out;
}

function paeth(a, b, c) {
	const p = a + b - c;
	const pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
	if (pa <= pb && pa <= pc) return a;
	return pb <= pc ? b : c;
}

/** Encode RGBA8 pixels as a PNG buffer (filter 0 on every row). */
export function encodePng(width, height, rgba) {
	const stride = width * 4;
	const raw = Buffer.alloc(height * (stride + 1));
	for (let y = 0; y < height; y++) {
		raw[y * (stride + 1)] = 0;
		rgba.copy(raw, y * (stride + 1) + 1, y * stride, (y + 1) * stride);
	}

	const ihdr = Buffer.alloc(13);
	ihdr.writeUInt32BE(width, 0);
	ihdr.writeUInt32BE(height, 4);
	ihdr[8] = 8; // bit depth
	ihdr[9] = 6; // color type: RGBA
	// 10: compression, 11: filter, 12: interlace — all 0.

	return Buffer.concat([
		PNG_SIGNATURE,
		chunk('IHDR', ihdr),
		chunk('IDAT', deflateSync(raw, { level: 6 })),
		chunk('IEND', Buffer.alloc(0)),
	]);
}

function chunk(type, data) {
	const out = Buffer.alloc(12 + data.length);
	out.writeUInt32BE(data.length, 0);
	out.write(type, 4, 'ascii');
	data.copy(out, 8);
	out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
	return out;
}

const CRC_TABLE = (() => {
	const table = new Int32Array(256);
	for (let n = 0; n < 256; n++) {
		let c = n;
		for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
		table[n] = c;
	}
	return table;
})();

function crc32(buf) {
	let c = -1;
	for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
	return (c ^ -1) >>> 0;
}
