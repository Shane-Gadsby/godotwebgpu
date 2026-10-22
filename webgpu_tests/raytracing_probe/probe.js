// Software ray tracing probe: how many rays per second a WGSL BVH traversal reaches on this
// adapter, through the browser. Standalone -- no engine, no Godot; a CPU-built binned-SAH BVH2
// over a procedural scene, traversed by a compute shader with a per-thread stack, which is the
// shape any software ray tracing in the WebGPU Forward+ renderer would take (WebGPU has no
// hardware ray tracing; see webgpu_notes/TASKS.md §2.34). Three ray kinds are timed:
//   primary  -- coherent camera rays, closest hit            (1 ray per pixel)
//   shadow   -- rays from each primary hit to a point light, any hit (1 per hit pixel)
//   bounce   -- cosine-weighted hemisphere rays from each hit, closest hit (incoherent, 1 per hit)
// Query parameters: w, h (resolution), tris (comma list of triangle targets), iters (timed
// dispatches per kernel). Results land in window.__probeResult and a [PROBE-RESULT] console line.

const qs = new URLSearchParams(location.search);
const WIDTH = Math.max(8, +(qs.get('w') || 1920));
const HEIGHT = Math.max(8, +(qs.get('h') || 1080));
const TRI_TARGETS = (qs.get('tris') || '100000,1000000').split(',').map(Number).filter((n) => n > 0);
const ITERS = Math.max(1, +(qs.get('iters') || 20));
const WARMUP = 3;
const LEAF_MAX = 4;
const STACK_SIZE = 48; // per-thread traversal stack; the build caps depth below it
const MAX_DEPTH = STACK_SIZE - 4;
const MISS = 0xffffffff;
const VERIFY_SAMPLES = Math.max(0, +(qs.get('verify') ?? 64)); // primary rays checked against a CPU brute-force trace

const logEl = document.getElementById('log');
function log(...a) {
	const line = a.join(' ');
	console.log('[probe] ' + line);
	if (logEl) {
		logEl.textContent += line + '\n';
	}
}

function mulberry32(seed) {
	return function () {
		seed |= 0;
		seed = (seed + 0x6D2B79F5) | 0;
		let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
		t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
		return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
	};
}

// ---------------------------------------------------------------------------------------------
// Scene: a subdivided ground plane and a grid of tessellated spheres and boxes, tessellated so
// the total lands near the requested triangle count. Deterministic.
// ---------------------------------------------------------------------------------------------
function makeScene(targetTris) {
	const rnd = mulberry32(1234);
	const tris = [];
	const push = (a, b, c) => {
		tris.push(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
	};
	const objCount = Math.max(9, Math.round(Math.sqrt(targetTris / 1200)) ** 2);
	const side = Math.round(Math.sqrt(objCount));
	const spacing = 6;
	const half = (side + 1) * spacing * 0.5;

	// Ground: (side*2)^2 quads.
	const gn = side * 2;
	for (let i = 0; i < gn; i++) {
		for (let j = 0; j < gn; j++) {
			const x0 = -half + (i / gn) * 2 * half;
			const x1 = -half + ((i + 1) / gn) * 2 * half;
			const z0 = -half + (j / gn) * 2 * half;
			const z1 = -half + ((j + 1) / gn) * 2 * half;
			push([x0, 0, z0], [x1, 0, z0], [x1, 0, z1]);
			push([x0, 0, z0], [x1, 0, z1], [x0, 0, z1]);
		}
	}
	const groundTris = tris.length / 9;
	const perObj = Math.max(12, (targetTris - groundTris) / objCount);

	let k = 0;
	for (let i = 0; i < side; i++) {
		for (let j = 0; j < side; j++) {
			if (k++ >= objCount) {
				break;
			}
			const cx = -half + (i + 1) * spacing + (rnd() - 0.5) * 2;
			const cz = -half + (j + 1) * spacing + (rnd() - 0.5) * 2;
			const r = 0.8 + rnd() * 1.4;
			if ((i + j) % 4 === 3) {
				// Box, kxk quads per face: 12k^2 triangles.
				const kk = Math.max(1, Math.round(Math.sqrt(perObj / 12)));
				const cy = r;
				const corner = (u, v, w) => [cx + u * r, cy + v * r, cz + w * r];
				const faces = [
					[[-1, -1, 1], [1, -1, 1], [-1, 1, 1]], // +z
					[[1, -1, -1], [-1, -1, -1], [1, 1, -1]], // -z
					[[1, -1, 1], [1, -1, -1], [1, 1, 1]], // +x
					[[-1, -1, -1], [-1, -1, 1], [-1, 1, -1]], // -x
					[[-1, 1, 1], [1, 1, 1], [-1, 1, -1]], // +y
					[[-1, -1, -1], [1, -1, -1], [-1, -1, 1]], // -y
				];
				for (const [o, du, dv] of faces) {
					const eu = [du[0] - o[0], du[1] - o[1], du[2] - o[2]];
					const ev = [dv[0] - o[0], dv[1] - o[1], dv[2] - o[2]];
					const at = (s, t) => corner(o[0] + eu[0] * s + ev[0] * t, o[1] + eu[1] * s + ev[1] * t, o[2] + eu[2] * s + ev[2] * t);
					for (let a = 0; a < kk; a++) {
						for (let b = 0; b < kk; b++) {
							const p00 = at(a / kk, b / kk);
							const p10 = at((a + 1) / kk, b / kk);
							const p11 = at((a + 1) / kk, (b + 1) / kk);
							const p01 = at(a / kk, (b + 1) / kk);
							push(p00, p10, p11);
							push(p00, p11, p01);
						}
					}
				}
			} else {
				// UV sphere: segs x rings quads, 2 triangles each (the poles degenerate to one).
				const segs = Math.max(6, Math.round(Math.sqrt(perObj)));
				const rings = Math.max(3, segs >> 1);
				const cy = r;
				const pt = (ring, seg) => {
					const phi = (ring / rings) * Math.PI;
					const th = (seg / segs) * 2 * Math.PI;
					return [cx + r * Math.sin(phi) * Math.cos(th), cy + r * Math.cos(phi), cz + r * Math.sin(phi) * Math.sin(th)];
				};
				for (let a = 0; a < rings; a++) {
					for (let b = 0; b < segs; b++) {
						const p00 = pt(a, b);
						const p10 = pt(a + 1, b);
						const p11 = pt(a + 1, b + 1);
						const p01 = pt(a, b + 1);
						if (a > 0) {
							push(p00, p10, p11);
						}
						if (a < rings - 1) {
							push(p00, p11, p01);
						}
					}
				}
			}
		}
	}
	return { tris: new Float32Array(tris), half, objCount };
}

// Brute-force closest hit over the original, unordered triangle list, in double precision: the truth a
// sample of the GPU's primary rays is checked against. Same Moller-Trumbore as the shader.
function bruteForceClosest(tri, n, ro, rd, tmax) {
	let best = tmax;
	for (let i = 0; i < n; i++) {
		const o = i * 9;
		const v0x = tri[o];
		const v0y = tri[o + 1];
		const v0z = tri[o + 2];
		const e1x = tri[o + 3] - v0x;
		const e1y = tri[o + 4] - v0y;
		const e1z = tri[o + 5] - v0z;
		const e2x = tri[o + 6] - v0x;
		const e2y = tri[o + 7] - v0y;
		const e2z = tri[o + 8] - v0z;
		const hx = rd[1] * e2z - rd[2] * e2y;
		const hy = rd[2] * e2x - rd[0] * e2z;
		const hz = rd[0] * e2y - rd[1] * e2x;
		const a = e1x * hx + e1y * hy + e1z * hz;
		if (Math.abs(a) < 1e-12) {
			continue;
		}
		const f = 1 / a;
		const sx = ro[0] - v0x;
		const sy = ro[1] - v0y;
		const sz = ro[2] - v0z;
		const u = f * (sx * hx + sy * hy + sz * hz);
		if (u < 0 || u > 1) {
			continue;
		}
		const qx = sy * e1z - sz * e1y;
		const qy = sz * e1x - sx * e1z;
		const qz = sx * e1y - sy * e1x;
		const v = f * (rd[0] * qx + rd[1] * qy + rd[2] * qz);
		if (v < 0 || u + v > 1) {
			continue;
		}
		const t = f * (e2x * qx + e2y * qy + e2z * qz);
		if (t > 0 && t < best) {
			best = t;
		}
	}
	return best < tmax ? best : -1;
}

// ---------------------------------------------------------------------------------------------
// BVH: binned SAH, 2 children stored contiguously, leaves of up to LEAF_MAX triangles.
// Node (32 B): bmin.xyz, left_first (u32), bmax.xyz, count (u32; 0 = interior).
// Triangle (48 B): v0, edge1, edge2 as vec4 (w unused), reordered to leaf order.
// ---------------------------------------------------------------------------------------------
function buildBVH(tri, n) {
	const bmin = new Float32Array(n * 3);
	const bmax = new Float32Array(n * 3);
	const cent = new Float32Array(n * 3);
	for (let i = 0; i < n; i++) {
		for (let a = 0; a < 3; a++) {
			const p0 = tri[i * 9 + a];
			const p1 = tri[i * 9 + 3 + a];
			const p2 = tri[i * 9 + 6 + a];
			const lo = Math.min(p0, p1, p2);
			const hi = Math.max(p0, p1, p2);
			bmin[i * 3 + a] = lo;
			bmax[i * 3 + a] = hi;
			cent[i * 3 + a] = (lo + hi) * 0.5;
		}
	}
	const idx = new Uint32Array(n);
	for (let i = 0; i < n; i++) {
		idx[i] = i;
	}
	const maxNodes = 2 * n;
	const nodeBuf = new ArrayBuffer(maxNodes * 32);
	const nf = new Float32Array(nodeBuf);
	const nu = new Uint32Array(nodeBuf);
	let nodeCount = 1;
	let maxDepth = 0;
	let leafCount = 0;
	const BINS = 16;
	const binCount = new Uint32Array(BINS);
	const binMin = new Float32Array(BINS * 3);
	const binMax = new Float32Array(BINS * 3);
	const leftArea = new Float32Array(BINS);
	const leftCnt = new Uint32Array(BINS);
	const area = (mnx, mny, mnz, mxx, mxy, mxz) => {
		const dx = mxx - mnx;
		const dy = mxy - mny;
		const dz = mxz - mnz;
		return 2 * (dx * dy + dy * dz + dz * dx);
	};
	const stack = [[0, 0, n, 0]];
	while (stack.length) {
		const [ni, first, count, depth] = stack.pop();
		if (depth > maxDepth) {
			maxDepth = depth;
		}
		let mnx = Infinity;
		let mny = Infinity;
		let mnz = Infinity;
		let mxx = -Infinity;
		let mxy = -Infinity;
		let mxz = -Infinity;
		let cmnx = Infinity;
		let cmny = Infinity;
		let cmnz = Infinity;
		let cmxx = -Infinity;
		let cmxy = -Infinity;
		let cmxz = -Infinity;
		for (let i = first; i < first + count; i++) {
			const t = idx[i] * 3;
			mnx = Math.min(mnx, bmin[t]);
			mny = Math.min(mny, bmin[t + 1]);
			mnz = Math.min(mnz, bmin[t + 2]);
			mxx = Math.max(mxx, bmax[t]);
			mxy = Math.max(mxy, bmax[t + 1]);
			mxz = Math.max(mxz, bmax[t + 2]);
			cmnx = Math.min(cmnx, cent[t]);
			cmny = Math.min(cmny, cent[t + 1]);
			cmnz = Math.min(cmnz, cent[t + 2]);
			cmxx = Math.max(cmxx, cent[t]);
			cmxy = Math.max(cmxy, cent[t + 1]);
			cmxz = Math.max(cmxz, cent[t + 2]);
		}
		const o = ni * 8;
		nf[o] = mnx;
		nf[o + 1] = mny;
		nf[o + 2] = mnz;
		nf[o + 4] = mxx;
		nf[o + 5] = mxy;
		nf[o + 6] = mxz;
		const makeLeaf = () => {
			nu[o + 3] = first;
			nu[o + 7] = count;
			leafCount++;
		};
		if (count <= LEAF_MAX || depth >= MAX_DEPTH) {
			makeLeaf();
			continue;
		}
		const ext = [cmxx - cmnx, cmxy - cmny, cmxz - cmnz];
		let axis = 0;
		if (ext[1] > ext[axis]) {
			axis = 1;
		}
		if (ext[2] > ext[axis]) {
			axis = 2;
		}
		let split = -1; // index into idx where the right child starts
		if (ext[axis] > 1e-9) {
			binCount.fill(0);
			binMin.fill(Infinity);
			binMax.fill(-Infinity);
			const cmin = [cmnx, cmny, cmnz][axis];
			const scale = BINS / ext[axis];
			for (let i = first; i < first + count; i++) {
				const t = idx[i] * 3;
				let b = Math.floor((cent[t + axis] - cmin) * scale);
				if (b >= BINS) {
					b = BINS - 1;
				}
				binCount[b]++;
				for (let a = 0; a < 3; a++) {
					binMin[b * 3 + a] = Math.min(binMin[b * 3 + a], bmin[t + a]);
					binMax[b * 3 + a] = Math.max(binMax[b * 3 + a], bmax[t + a]);
				}
			}
			// Left-to-right prefix (area, count) at each candidate plane, then a right-to-left sweep.
			let lx0 = Infinity;
			let ly0 = Infinity;
			let lz0 = Infinity;
			let lx1 = -Infinity;
			let ly1 = -Infinity;
			let lz1 = -Infinity;
			let lc = 0;
			for (let b = 0; b < BINS - 1; b++) {
				if (binCount[b]) {
					lx0 = Math.min(lx0, binMin[b * 3]);
					ly0 = Math.min(ly0, binMin[b * 3 + 1]);
					lz0 = Math.min(lz0, binMin[b * 3 + 2]);
					lx1 = Math.max(lx1, binMax[b * 3]);
					ly1 = Math.max(ly1, binMax[b * 3 + 1]);
					lz1 = Math.max(lz1, binMax[b * 3 + 2]);
				}
				lc += binCount[b];
				leftCnt[b] = lc;
				leftArea[b] = lc ? area(lx0, ly0, lz0, lx1, ly1, lz1) : 0;
			}
			let rx0 = Infinity;
			let ry0 = Infinity;
			let rz0 = Infinity;
			let rx1 = -Infinity;
			let ry1 = -Infinity;
			let rz1 = -Infinity;
			let rc = 0;
			let bestCost = Infinity;
			let bestBin = -1;
			for (let b = BINS - 1; b >= 1; b--) {
				if (binCount[b]) {
					rx0 = Math.min(rx0, binMin[b * 3]);
					ry0 = Math.min(ry0, binMin[b * 3 + 1]);
					rz0 = Math.min(rz0, binMin[b * 3 + 2]);
					rx1 = Math.max(rx1, binMax[b * 3]);
					ry1 = Math.max(ry1, binMax[b * 3 + 1]);
					rz1 = Math.max(rz1, binMax[b * 3 + 2]);
				}
				rc += binCount[b];
				const lcnt = leftCnt[b - 1];
				if (lcnt === 0 || rc === 0) {
					continue;
				}
				const cost = lcnt * leftArea[b - 1] + rc * area(rx0, ry0, rz0, rx1, ry1, rz1);
				if (cost < bestCost) {
					bestCost = cost;
					bestBin = b;
				}
			}
			const parentArea = area(mnx, mny, mnz, mxx, mxy, mxz);
			if (bestBin >= 0 && (bestCost < count * parentArea || count > 4 * LEAF_MAX)) {
				// Partition in place: bins < bestBin to the left.
				let i = first;
				let j = first + count - 1;
				while (i <= j) {
					const t = idx[i] * 3;
					let b = Math.floor((cent[t + axis] - cmin) * scale);
					if (b >= BINS) {
						b = BINS - 1;
					}
					if (b < bestBin) {
						i++;
					} else {
						const tmp = idx[i];
						idx[i] = idx[j];
						idx[j] = tmp;
						j--;
					}
				}
				split = i;
			} else if (bestBin < 0 && count <= 4 * LEAF_MAX) {
				makeLeaf();
				continue;
			} else {
				split = -1;
			}
		}
		if (split <= first || split >= first + count) {
			if (count <= 4 * LEAF_MAX) {
				makeLeaf();
				continue;
			}
			// Degenerate centroids: median split by index order.
			split = first + (count >> 1);
		}
		const left = nodeCount;
		nodeCount += 2;
		nu[o + 3] = left;
		nu[o + 7] = 0;
		stack.push([left, first, split - first, depth + 1]);
		stack.push([left + 1, split, first + count - split, depth + 1]);
	}
	const out = new Float32Array(n * 12);
	for (let k = 0; k < n; k++) {
		const t = idx[k] * 9;
		for (let a = 0; a < 3; a++) {
			const v0 = tri[t + a];
			out[k * 12 + a] = v0;
			out[k * 12 + 4 + a] = tri[t + 3 + a] - v0;
			out[k * 12 + 8 + a] = tri[t + 6 + a] - v0;
		}
	}
	return { nodes: new Float32Array(nodeBuf, 0, nodeCount * 8), tris: out, nodeCount, leafCount, maxDepth };
}

// ---------------------------------------------------------------------------------------------
// WGSL
// ---------------------------------------------------------------------------------------------
const SHADER = /* wgsl */ `
struct Node { bmin: vec3f, left_first: u32, bmax: vec3f, count: u32 };
struct Tri { v0: vec4f, e1: vec4f, e2: vec4f };
struct Params {
	cam_pos: vec4f,
	cam_fwd: vec4f,
	cam_right: vec4f,
	cam_up: vec4f,
	light_pos: vec4f,
	size: vec2u,
	frame: u32,
	mode: u32,
	tmax: f32,
	eps: f32,
	pad0: f32,
	pad1: f32,
};
@group(0) @binding(0) var<storage, read> nodes: array<Node>;
@group(0) @binding(1) var<storage, read> tris: array<Tri>;
@group(0) @binding(2) var<uniform> params: Params;
@group(0) @binding(3) var<storage, read_write> hit_pos: array<vec4f>;   // xyz hit point, w = t (< 0: miss)
@group(0) @binding(4) var<storage, read_write> hit_nrm: array<vec4f>;   // xyz normal, w = traversal steps (u32 bits)
@group(0) @binding(5) var<storage, read_write> results: array<vec2f>;   // x = shadow visibility or bounce t, y = steps bits

const MISS: u32 = 0xffffffffu;
const STACK_SIZE: u32 = ${STACK_SIZE}u;

struct Hit { t: f32, prim: u32, steps: u32 };

fn aabb_dist(ro: vec3f, inv: vec3f, bmin: vec3f, bmax: vec3f, tmax: f32) -> f32 {
	let t1 = (bmin - ro) * inv;
	let t2 = (bmax - ro) * inv;
	let tmn = min(t1, t2);
	let tmx = max(t1, t2);
	let tn = max(max(tmn.x, tmn.y), max(tmn.z, 0.0));
	let tf = min(min(tmx.x, tmx.y), min(tmx.z, tmax));
	return select(1e30, tn, tf >= tn);
}

// Moller-Trumbore; returns t or 1e30.
fn tri_dist(ro: vec3f, rd: vec3f, tr: Tri, tmax: f32) -> f32 {
	let e1 = tr.e1.xyz;
	let e2 = tr.e2.xyz;
	let h = cross(rd, e2);
	let a = dot(e1, h);
	if (abs(a) < 1e-9) { return 1e30; }
	let f = 1.0 / a;
	let s = ro - tr.v0.xyz;
	let u = f * dot(s, h);
	if (u < 0.0 || u > 1.0) { return 1e30; }
	let q = cross(s, e1);
	let v = f * dot(rd, q);
	if (v < 0.0 || u + v > 1.0) { return 1e30; }
	let t = f * dot(e2, q);
	return select(1e30, t, t > 0.0 && t < tmax);
}

fn trace(ro: vec3f, rd: vec3f, tmax_in: f32, any_hit: bool) -> Hit {
	var h: Hit;
	h.t = tmax_in;
	h.prim = MISS;
	h.steps = 0u;
	let rd_safe = select(rd, vec3f(1e-20), abs(rd) < vec3f(1e-20));
	let inv = 1.0 / rd_safe;
	var stack: array<u32, STACK_SIZE>;
	var sp: u32 = 0u;
	var node: u32 = 0u;
	loop {
		h.steps += 1u;
		let n = nodes[node];
		if (n.count > 0u) {
			for (var i = 0u; i < n.count; i++) {
				let p = n.left_first + i;
				let t = tri_dist(ro, rd, tris[p], h.t);
				if (t < h.t) {
					h.t = t;
					h.prim = p;
					if (any_hit) { return h; }
				}
			}
			if (sp == 0u) { break; }
			sp -= 1u;
			node = stack[sp];
			continue;
		}
		let c1 = n.left_first;
		let c2 = c1 + 1u;
		let n1 = nodes[c1];
		let n2 = nodes[c2];
		var d1 = aabb_dist(ro, inv, n1.bmin, n1.bmax, h.t);
		var d2 = aabb_dist(ro, inv, n2.bmin, n2.bmax, h.t);
		var near = c1;
		var far = c2;
		if (d1 > d2) {
			let td = d1; d1 = d2; d2 = td;
			near = c2; far = c1;
		}
		if (d1 >= h.t) {
			if (sp == 0u) { break; }
			sp -= 1u;
			node = stack[sp];
		} else {
			node = near;
			if (d2 < h.t && sp < STACK_SIZE) {
				stack[sp] = far;
				sp += 1u;
			}
		}
	}
	return h;
}

fn pcg(v: u32) -> u32 {
	let s = v * 747796405u + 2891336453u;
	let w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
	return (w >> 22u) ^ w;
}
fn rand01(seed: ptr<function, u32>) -> f32 {
	*seed = pcg(*seed);
	return f32(*seed) * (1.0 / 4294967296.0);
}
fn cosine_hemisphere(n: vec3f, r1: f32, r2: f32) -> vec3f {
	let a = select(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), abs(n.x) > 0.9);
	let t = normalize(cross(a, n));
	let b = cross(n, t);
	let phi = 6.2831853 * r1;
	let r = sqrt(r2);
	return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - r2)));
}

@compute @workgroup_size(8, 8)
fn primary(@builtin(global_invocation_id) gid: vec3u) {
	if (gid.x >= params.size.x || gid.y >= params.size.y) { return; }
	let px = gid.y * params.size.x + gid.x;
	let uv = (vec2f(gid.xy) + vec2f(0.5)) / vec2f(params.size) * 2.0 - vec2f(1.0);
	let rd = normalize(params.cam_fwd.xyz + uv.x * params.cam_right.xyz - uv.y * params.cam_up.xyz);
	let h = trace(params.cam_pos.xyz, rd, params.tmax, false);
	if (h.prim == MISS) {
		hit_pos[px] = vec4f(0.0, 0.0, 0.0, -1.0);
		hit_nrm[px] = vec4f(0.0, 0.0, 0.0, bitcast<f32>(h.steps));
		return;
	}
	let tr = tris[h.prim];
	var n = normalize(cross(tr.e1.xyz, tr.e2.xyz));
	if (dot(n, rd) > 0.0) { n = -n; }
	hit_pos[px] = vec4f(params.cam_pos.xyz + rd * h.t, h.t);
	hit_nrm[px] = vec4f(n, bitcast<f32>(h.steps));
}

@compute @workgroup_size(8, 8)
fn shadow(@builtin(global_invocation_id) gid: vec3u) {
	if (gid.x >= params.size.x || gid.y >= params.size.y) { return; }
	let px = gid.y * params.size.x + gid.x;
	let hp = hit_pos[px];
	if (hp.w < 0.0) { results[px] = vec2f(-1.0, 0.0); return; }
	let n = hit_nrm[px].xyz;
	let o = hp.xyz + n * params.eps;
	let to_l = params.light_pos.xyz - o;
	let dist = length(to_l);
	let h = trace(o, to_l / dist, dist, true);
	results[px] = vec2f(select(1.0, 0.0, h.prim != MISS), bitcast<f32>(h.steps));
}

@compute @workgroup_size(8, 8)
fn bounce(@builtin(global_invocation_id) gid: vec3u) {
	if (gid.x >= params.size.x || gid.y >= params.size.y) { return; }
	let px = gid.y * params.size.x + gid.x;
	let hp = hit_pos[px];
	if (hp.w < 0.0) { results[px] = vec2f(-1.0, 0.0); return; }
	let n = hit_nrm[px].xyz;
	var seed = pcg(px * 9781u + params.frame * 6271u + 1u);
	let r1 = rand01(&seed);
	let r2 = rand01(&seed);
	let rd = cosine_hemisphere(n, r1, r2);
	let o = hp.xyz + n * params.eps;
	let h = trace(o, rd, params.tmax, false);
	results[px] = vec2f(select(-1.0, h.t, h.prim != MISS), bitcast<f32>(h.steps));
}
`;

// Fullscreen view of the last results: normals, darkened where the shadow ray was blocked.
const VIEW_SHADER = /* wgsl */ `
struct Params {
	cam_pos: vec4f, cam_fwd: vec4f, cam_right: vec4f, cam_up: vec4f, light_pos: vec4f,
	size: vec2u, frame: u32, mode: u32, tmax: f32, eps: f32, pad0: f32, pad1: f32,
};
@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var<storage, read> hit_pos: array<vec4f>;
@group(0) @binding(2) var<storage, read> hit_nrm: array<vec4f>;
@group(0) @binding(3) var<storage, read> results: array<vec2f>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
	let p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
	return vec4f(p[i], 0.0, 1.0);
}
@fragment fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
	let p = vec2u(pos.xy);
	if (p.x >= params.size.x || p.y >= params.size.y) { return vec4f(0.0, 0.0, 0.0, 1.0); }
	let px = p.y * params.size.x + p.x;
	let hp = hit_pos[px];
	if (hp.w < 0.0) { return vec4f(0.35, 0.5, 0.75, 1.0); }
	let n = hit_nrm[px].xyz;
	let l = normalize(params.light_pos.xyz - hp.xyz);
	let vis = select(0.25, 1.0, results[px].x > 0.5);
	let shade = (0.15 + 0.85 * max(0.0, dot(n, l))) * vis;
	let albedo = vec3f(0.5) + 0.5 * n;
	return vec4f(albedo * shade, 1.0);
}
`;

// ---------------------------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------------------------
async function main() {
	const result = { ok: false, resolution: [WIDTH, HEIGHT], iters: ITERS, scenes: [], checks: [] };
	const check = (scene, name, pass, detail) => {
		result.checks.push({ scene, name, pass: !!pass, detail });
		if (!pass) {
			log(`CHECK FAILED [${scene}] ${name}: ${detail}`);
		}
	};
	window.__probeResult = result;
	try {
		if (!navigator.gpu) {
			throw new Error('navigator.gpu missing (not a secure context, or WebGPU disabled)');
		}
		const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
		if (!adapter) {
			throw new Error('no WebGPU adapter');
		}
		const info = adapter.info || {};
		result.adapter = {
			vendor: info.vendor || '',
			architecture: info.architecture || '',
			description: info.description || '',
			device: info.device || '',
			isFallbackAdapter: !!(info.isFallbackAdapter || adapter.isFallbackAdapter),
			subgroupMinSize: info.subgroupMinSize,
			subgroupMaxSize: info.subgroupMaxSize,
		};
		result.features = [...adapter.features].sort();
		const L = adapter.limits;
		result.limits = {};
		for (const k of ['maxStorageBuffersPerShaderStage', 'maxStorageBufferBindingSize', 'maxBufferSize', 'maxUniformBufferBindingSize',
			'maxStorageTexturesPerShaderStage', 'maxSampledTexturesPerShaderStage', 'maxBindGroups', 'maxComputeInvocationsPerWorkgroup',
			'maxComputeWorkgroupStorageSize', 'maxComputeWorkgroupsPerDimension', 'maxComputeWorkgroupSizeX']) {
			result.limits[k] = L[k];
		}
		const hasTimestamps = adapter.features.has('timestamp-query');
		const device = await adapter.requestDevice({
			requiredFeatures: hasTimestamps ? ['timestamp-query'] : [],
			requiredLimits: {
				maxStorageBufferBindingSize: L.maxStorageBufferBindingSize,
				maxBufferSize: L.maxBufferSize,
			},
		});
		device.addEventListener('uncapturederror', (e) => {
			log('UNCAPTURED ERROR: ' + (e.error && e.error.message));
			result.errors = result.errors || [];
			result.errors.push(String(e.error && e.error.message));
		});
		log(`adapter: ${result.adapter.vendor} / ${result.adapter.architecture} ${result.adapter.description} fallback=${result.adapter.isFallbackAdapter}`);
		log(`timestamps: ${hasTimestamps}  subgroups: ${adapter.features.has('subgroups')} (${info.subgroupMinSize}-${info.subgroupMaxSize})  f16: ${adapter.features.has('shader-f16')}`);
		log(`maxStorageBufferBindingSize ${L.maxStorageBufferBindingSize}  maxBufferSize ${L.maxBufferSize}  storage buffers/stage ${L.maxStorageBuffersPerShaderStage}`);
		log(`resolution ${WIDTH}x${HEIGHT}, ${ITERS} timed dispatches per kernel`);

		const module = device.createShaderModule({ code: SHADER });
		const compInfo = await module.getCompilationInfo();
		for (const m of compInfo.messages) {
			if (m.type === 'error') {
				throw new Error(`WGSL ${m.type} ${m.lineNum}:${m.linePos} ${m.message}`);
			}
		}
		const bgl = device.createBindGroupLayout({
			entries: [
				{ binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
				{ binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
				{ binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } },
				{ binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
				{ binding: 4, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
				{ binding: 5, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
			],
		});
		const layout = device.createPipelineLayout({ bindGroupLayouts: [bgl] });
		const pipelines = {};
		for (const ep of ['primary', 'shadow', 'bounce']) {
			pipelines[ep] = await device.createComputePipelineAsync({ layout, compute: { module, entryPoint: ep } });
		}

		// Canvas view (optional: only when the page has one).
		const canvas = document.getElementById('view');
		let view = null;
		if (canvas) {
			canvas.width = WIDTH;
			canvas.height = HEIGHT;
			const ctx = canvas.getContext('webgpu');
			const format = navigator.gpu.getPreferredCanvasFormat();
			ctx.configure({ device, format, alphaMode: 'opaque' });
			const vmod = device.createShaderModule({ code: VIEW_SHADER });
			const vbgl = device.createBindGroupLayout({
				entries: [
					{ binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
					{ binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'read-only-storage' } },
					{ binding: 2, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'read-only-storage' } },
					{ binding: 3, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'read-only-storage' } },
				],
			});
			const vpipe = await device.createRenderPipelineAsync({
				layout: device.createPipelineLayout({ bindGroupLayouts: [vbgl] }),
				vertex: { module: vmod, entryPoint: 'vs' },
				fragment: { module: vmod, entryPoint: 'fs', targets: [{ format }] },
				primitive: { topology: 'triangle-list' },
			});
			view = { ctx, vbgl, vpipe };
		}

		const pixels = WIDTH * HEIGHT;
		const paramsBuf = device.createBuffer({ size: 112, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
		const hitPosBuf = device.createBuffer({ size: pixels * 16, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
		const hitNrmBuf = device.createBuffer({ size: pixels * 16, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
		const resultsBuf = device.createBuffer({ size: pixels * 8, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
		const readPos = device.createBuffer({ size: pixels * 16, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
		const readNrm = device.createBuffer({ size: pixels * 16, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
		const readRes = device.createBuffer({ size: pixels * 8, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
		let querySet = null;
		let queryResolve = null;
		let queryRead = null;
		if (hasTimestamps) {
			querySet = device.createQuerySet({ type: 'timestamp', count: 2 * ITERS });
			queryResolve = device.createBuffer({ size: 16 * ITERS, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
			queryRead = device.createBuffer({ size: 16 * ITERS, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
		}
		const groupsX = Math.ceil(WIDTH / 8);
		const groupsY = Math.ceil(HEIGHT / 8);

		async function readback(src, dst, size) {
			const enc = device.createCommandEncoder();
			enc.copyBufferToBuffer(src, 0, dst, 0, size);
			device.queue.submit([enc.finish()]);
			await dst.mapAsync(GPUMapMode.READ);
			const copy = dst.getMappedRange().slice(0);
			dst.unmap();
			return copy;
		}

		async function timeKernel(name, bindGroup, frameBase) {
			const writeParams = (frame) => {
				paramsU32[22] = frame;
				device.queue.writeBuffer(paramsBuf, 0, paramsData);
			};
			for (let i = 0; i < WARMUP; i++) {
				writeParams(frameBase + i);
				const enc = device.createCommandEncoder();
				const pass = enc.beginComputePass();
				pass.setPipeline(pipelines[name]);
				pass.setBindGroup(0, bindGroup);
				pass.dispatchWorkgroups(groupsX, groupsY);
				pass.end();
				device.queue.submit([enc.finish()]);
			}
			await device.queue.onSubmittedWorkDone();
			writeParams(frameBase + WARMUP);
			const t0 = performance.now();
			const enc = device.createCommandEncoder();
			for (let i = 0; i < ITERS; i++) {
				const desc = {};
				if (querySet) {
					desc.timestampWrites = { querySet, beginningOfPassWriteIndex: 2 * i, endOfPassWriteIndex: 2 * i + 1 };
				}
				const pass = enc.beginComputePass(desc);
				pass.setPipeline(pipelines[name]);
				pass.setBindGroup(0, bindGroup);
				pass.dispatchWorkgroups(groupsX, groupsY);
				pass.end();
			}
			if (querySet) {
				enc.resolveQuerySet(querySet, 0, 2 * ITERS, queryResolve, 0);
				enc.copyBufferToBuffer(queryResolve, 0, queryRead, 0, 16 * ITERS);
			}
			device.queue.submit([enc.finish()]);
			await device.queue.onSubmittedWorkDone();
			const wallMs = (performance.now() - t0) / ITERS;
			const out = { wall_ms: wallMs };
			if (querySet) {
				await queryRead.mapAsync(GPUMapMode.READ);
				const ts = new BigUint64Array(queryRead.getMappedRange().slice(0));
				queryRead.unmap();
				const samples = [];
				for (let i = 0; i < ITERS; i++) {
					samples.push(Number(ts[2 * i + 1] - ts[2 * i]) / 1e6);
				}
				samples.sort((a, b) => a - b);
				out.ts_ms_median = samples[samples.length >> 1];
				out.ts_ms_min = samples[0];
			}
			out.ms = out.ts_ms_median !== undefined && out.ts_ms_median > 0 ? out.ts_ms_median : wallMs;
			return out;
		}

		const paramsData = new ArrayBuffer(112);
		const paramsF32 = new Float32Array(paramsData);
		const paramsU32 = new Uint32Array(paramsData);

		for (const target of TRI_TARGETS) {
			log(`--- scene target ${target} triangles ---`);
			const tScene = performance.now();
			const scene = makeScene(target);
			const triCount = scene.tris.length / 9;
			const tBuild = performance.now();
			const bvh = buildBVH(scene.tris, triCount);
			const buildMs = performance.now() - tBuild;
			log(`${triCount} triangles, ${bvh.nodeCount} nodes (${bvh.leafCount} leaves), depth ${bvh.maxDepth}, scene gen ${(tBuild - tScene).toFixed(0)} ms, BVH build ${buildMs.toFixed(0)} ms`);
			if (bvh.maxDepth + 1 > STACK_SIZE) {
				log(`WARNING: BVH depth ${bvh.maxDepth} exceeds the traversal stack (${STACK_SIZE})`);
			}
			const nodesBuf = device.createBuffer({ size: bvh.nodes.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
			device.queue.writeBuffer(nodesBuf, 0, bvh.nodes);
			const trisBuf = device.createBuffer({ size: bvh.tris.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
			device.queue.writeBuffer(trisBuf, 0, bvh.tris);

			// Camera: above and to the side, looking at the middle of the field; 60 degree vertical fov.
			const half = scene.half;
			const camPos = [-0.75 * half, 0.55 * half, 0.9 * half];
			const target3 = [0, 0.5, 0];
			let fwd = [target3[0] - camPos[0], target3[1] - camPos[1], target3[2] - camPos[2]];
			const fl = Math.hypot(...fwd);
			fwd = fwd.map((v) => v / fl);
			let right = [fwd[2], 0, -fwd[0]];
			const rl = Math.hypot(...right);
			right = right.map((v) => v / rl);
			const up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]];
			const tanHalf = Math.tan((60 * Math.PI) / 360);
			const aspect = WIDTH / HEIGHT;
			paramsF32.set([...camPos, 0], 0);
			paramsF32.set([...fwd, 0], 4);
			paramsF32.set([right[0] * tanHalf * aspect, right[1] * tanHalf * aspect, right[2] * tanHalf * aspect, 0], 8);
			paramsF32.set([up[0] * tanHalf, up[1] * tanHalf, up[2] * tanHalf, 0], 12);
			paramsF32.set([0.2 * half, 1.1 * half, 0.1 * half, 0], 16);
			paramsU32[20] = WIDTH;
			paramsU32[21] = HEIGHT;
			paramsU32[22] = 0;
			paramsU32[23] = 0;
			paramsF32[24] = 8 * half; // tmax
			paramsF32[25] = 1e-3 * half; // eps
			device.queue.writeBuffer(paramsBuf, 0, paramsData);

			const bindGroup = device.createBindGroup({
				layout: bgl,
				entries: [
					{ binding: 0, resource: { buffer: nodesBuf } },
					{ binding: 1, resource: { buffer: trisBuf } },
					{ binding: 2, resource: { buffer: paramsBuf } },
					{ binding: 3, resource: { buffer: hitPosBuf } },
					{ binding: 4, resource: { buffer: hitNrmBuf } },
					{ binding: 5, resource: { buffer: resultsBuf } },
				],
			});

			const entry = { target, triangles: triCount, nodes: bvh.nodeCount, leaves: bvh.leafCount, depth: bvh.maxDepth, build_ms: buildMs, buffers_mb: (bvh.nodes.byteLength + bvh.tris.byteLength) / 1048576 };

			// Primary.
			const prim = await timeKernel('primary', bindGroup, 0);
			const posData = new Float32Array(await readback(hitPosBuf, readPos, pixels * 16));
			const nrmU32 = new Uint32Array(await readback(hitNrmBuf, readNrm, pixels * 16));
			let hits = 0;
			let steps = 0;
			let maxSteps = 0;
			for (let i = 0; i < pixels; i++) {
				if (posData[i * 4 + 3] >= 0) {
					hits++;
				}
				const s = nrmU32[i * 4 + 3];
				steps += s;
				if (s > maxSteps) {
					maxSteps = s;
				}
			}
			entry.primary = { ...prim, rays: pixels, mrays_per_s: pixels / prim.ms / 1e3, hit_fraction: hits / pixels, mean_steps: steps / pixels, max_steps: maxSteps };
			log(`primary  ${prim.ms.toFixed(3)} ms  ${entry.primary.mrays_per_s.toFixed(1)} Mrays/s  hits ${(100 * hits / pixels).toFixed(1)}%  steps mean ${(steps / pixels).toFixed(1)} max ${maxSteps}`);

			// Check a grid of primary rays against the brute-force CPU trace.
			if (VERIFY_SAMPLES > 0) {
				const tv = performance.now();
				const tmax = paramsF32[24];
				const cols = Math.max(1, Math.round(Math.sqrt(VERIFY_SAMPLES * aspect)));
				const rows = Math.max(1, Math.round(VERIFY_SAMPLES / cols));
				const tolAbs = 2e-4 * half;
				let mismatches = 0;
				let maxErr = 0;
				let checked = 0;
				for (let r = 0; r < rows; r++) {
					for (let c = 0; c < cols; c++) {
						const px = Math.min(WIDTH - 1, Math.floor(((c + 0.5) / cols) * WIDTH));
						const py = Math.min(HEIGHT - 1, Math.floor(((r + 0.5) / rows) * HEIGHT));
						const uvx = ((px + 0.5) / WIDTH) * 2 - 1;
						const uvy = ((py + 0.5) / HEIGHT) * 2 - 1;
						const rdx = fwd[0] + uvx * right[0] * tanHalf * aspect - uvy * up[0] * tanHalf;
						const rdy = fwd[1] + uvx * right[1] * tanHalf * aspect - uvy * up[1] * tanHalf;
						const rdz = fwd[2] + uvx * right[2] * tanHalf * aspect - uvy * up[2] * tanHalf;
						const len = Math.hypot(rdx, rdy, rdz);
						const tCpu = bruteForceClosest(scene.tris, triCount, camPos, [rdx / len, rdy / len, rdz / len], tmax);
						const tGpu = posData[(py * WIDTH + px) * 4 + 3];
						checked++;
						if (tCpu < 0 && tGpu < 0) {
							continue;
						}
						if (tCpu < 0 || tGpu < 0) {
							mismatches++;
							continue;
						}
						const err = Math.abs(tCpu - tGpu);
						maxErr = Math.max(maxErr, err);
						if (err > tolAbs + 1e-4 * tCpu) {
							mismatches++;
						}
					}
				}
				entry.verify = { samples: checked, mismatches, max_abs_t_error: maxErr, ms: performance.now() - tv };
				log(`verify   ${checked} primary rays vs brute force: ${mismatches} mismatches, max |dt| ${maxErr.toExponential(2)} (${entry.verify.ms.toFixed(0)} ms)`);
				// One edge-grazing ray in a sample may legitimately land differently in f32 and f64.
				check(target, 'primary traversal matches brute force', mismatches <= Math.max(1, Math.floor(checked * 0.02)), `${mismatches}/${checked} mismatches, max |dt| ${maxErr}`);
			}

			// Shadow (any hit).
			const sh = await timeKernel('shadow', bindGroup, 100);
			let resData = new Float32Array(await readback(resultsBuf, readRes, pixels * 8));
			let resU32 = new Uint32Array(resData.buffer);
			let occluded = 0;
			steps = 0;
			maxSteps = 0;
			for (let i = 0; i < pixels; i++) {
				if (resData[i * 2] < 0) {
					continue;
				}
				if (resData[i * 2] < 0.5) {
					occluded++;
				}
				const s = resU32[i * 2 + 1];
				steps += s;
				if (s > maxSteps) {
					maxSteps = s;
				}
			}
			entry.shadow = { ...sh, rays: hits, mrays_per_s: hits / sh.ms / 1e3, occluded_fraction: hits ? occluded / hits : 0, mean_steps: hits ? steps / hits : 0, max_steps: maxSteps };
			log(`shadow   ${sh.ms.toFixed(3)} ms  ${entry.shadow.mrays_per_s.toFixed(1)} Mrays/s  occluded ${(100 * entry.shadow.occluded_fraction).toFixed(1)}%  steps mean ${entry.shadow.mean_steps.toFixed(1)} max ${maxSteps}`);
			if (view) {
				drawView(device, view, paramsBuf, hitPosBuf, hitNrmBuf, resultsBuf);
			}

			// Bounce (incoherent, closest hit).
			const bo = await timeKernel('bounce', bindGroup, 200);
			resData = new Float32Array(await readback(resultsBuf, readRes, pixels * 8));
			resU32 = new Uint32Array(resData.buffer);
			let bhits = 0;
			steps = 0;
			maxSteps = 0;
			for (let i = 0; i < pixels; i++) {
				if (posData[i * 4 + 3] < 0) {
					continue;
				}
				if (resData[i * 2] >= 0) {
					bhits++;
				}
				const s = resU32[i * 2 + 1];
				steps += s;
				if (s > maxSteps) {
					maxSteps = s;
				}
			}
			entry.bounce = { ...bo, rays: hits, mrays_per_s: hits / bo.ms / 1e3, hit_fraction: hits ? bhits / hits : 0, mean_steps: hits ? steps / hits : 0, max_steps: maxSteps };
			log(`bounce   ${bo.ms.toFixed(3)} ms  ${entry.bounce.mrays_per_s.toFixed(1)} Mrays/s  hits ${(100 * entry.bounce.hit_fraction).toFixed(1)}%  steps mean ${entry.bounce.mean_steps.toFixed(1)} max ${maxSteps}`);

			// What one 1080p-equivalent frame of "1 primary + 1 shadow + 1 bounce per pixel" would cost here.
			entry.frame_ms_1spp = prim.ms + sh.ms + bo.ms;
			log(`1 spp (primary+shadow+bounce) at ${WIDTH}x${HEIGHT}: ${entry.frame_ms_1spp.toFixed(2)} ms`);

			check(target, 'BVH depth fits the traversal stack', bvh.maxDepth + 1 <= STACK_SIZE, `depth ${bvh.maxDepth}, stack ${STACK_SIZE}`);
			check(target, 'primary hit fraction plausible for the fixed camera', entry.primary.hit_fraction > 0.45 && entry.primary.hit_fraction < 0.8, `${(100 * entry.primary.hit_fraction).toFixed(1)}% (expect ~61%)`);
			check(target, 'shadow occlusion plausible', entry.shadow.occluded_fraction > 0.05 && entry.shadow.occluded_fraction < 0.6, `${(100 * entry.shadow.occluded_fraction).toFixed(1)}% occluded (expect 20-30%)`);
			check(target, 'bounce hit fraction plausible', entry.bounce.hit_fraction > 0.05 && entry.bounce.hit_fraction < 0.7, `${(100 * entry.bounce.hit_fraction).toFixed(1)}% hit (expect 20-35%)`);
			check(target, 'mean traversal steps bounded', entry.primary.mean_steps < 80 && entry.shadow.mean_steps < 80 && entry.bounce.mean_steps < 80, `primary ${entry.primary.mean_steps.toFixed(1)}, shadow ${entry.shadow.mean_steps.toFixed(1)}, bounce ${entry.bounce.mean_steps.toFixed(1)}`);
			result.scenes.push(entry);
			nodesBuf.destroy();
			trisBuf.destroy();
		}
		result.ok = result.checks.every((c) => c.pass) && !(result.errors && result.errors.length) && result.scenes.length === TRI_TARGETS.length;
		log(`checks: ${result.checks.filter((c) => c.pass).length}/${result.checks.length} passed -> ${result.ok ? 'OK' : 'FAILED'}`);
	} catch (e) {
		result.error = String(e && e.stack || e);
		log('ERROR ' + result.error);
	}
	window.__probeDone = true;
	console.log('[PROBE-RESULT] ' + JSON.stringify(result));
}

function drawView(device, view, paramsBuf, hitPosBuf, hitNrmBuf, resultsBuf) {
	const bg = device.createBindGroup({
		layout: view.vbgl,
		entries: [
			{ binding: 0, resource: { buffer: paramsBuf } },
			{ binding: 1, resource: { buffer: hitPosBuf } },
			{ binding: 2, resource: { buffer: hitNrmBuf } },
			{ binding: 3, resource: { buffer: resultsBuf } },
		],
	});
	const enc = device.createCommandEncoder();
	const pass = enc.beginRenderPass({ colorAttachments: [{ view: view.ctx.getCurrentTexture().createView(), loadOp: 'clear', clearValue: { r: 0, g: 0, b: 0, a: 1 }, storeOp: 'store' }] });
	pass.setPipeline(view.vpipe);
	pass.setBindGroup(0, bg);
	pass.draw(3);
	pass.end();
	device.queue.submit([enc.finish()]);
}

main();
