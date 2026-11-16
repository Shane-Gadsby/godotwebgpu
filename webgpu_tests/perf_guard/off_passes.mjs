// Features this fork adds must cost nothing when a project does not use them (TASKS §2.66, work task 94): no pass of
// theirs may run in a scene that leaves them at their defaults. Times every pass with gpu_time.mjs --passes and fails
// on any pipeline from the list below.
//   node off_passes.mjs [--exports <scene_smoketest exports dir>] [--scenes a,b]
// Needs a real GPU adapter with 'timestamp-query' (WEBGPU_REAL_GPU=1, as local_ci.sh sets); exit 2 + SKIP otherwise.
import { spawnSync } from 'child_process';
import { existsSync } from 'fs';
import { dirname, join, resolve } from 'path';
import { fileURLToPath } from 'url';

const HERE = dirname(fileURLToPath(import.meta.url));
const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i !== -1 ? process.argv[i + 1] : d; };
const EXPORTS = resolve(arg('exports', join(HERE, '..', 'scene_smoketest', 'exports')));
// Default-settings scenes: three Forward+, two Mobile.
const SCENES = arg('scenes', 'demo_3d_lights_shadows,demo_3d_particles,demo_3d_platformer,benchmark_pbr,benchmark_postfx').split(',');
// Pipelines of features that are off unless a project turns them on (camera batch §4.47-4.59, light shafts, GTAO).
const OPT_IN = /^(Camera(Finish|Histogram|LocalExposure|Metering|MotionBlur)|LightShafts|ColorGrading|Gtao)ShaderRD:/;

if (!process.env.WEBGPU_REAL_GPU) {
	console.log('SKIP -- needs a real GPU adapter with timestamp queries (set WEBGPU_REAL_GPU=1)');
	process.exit(2);
}
let failed = 0;
let ran = 0;
for (const scene of SCENES) {
	const dir = join(EXPORTS, scene);
	if (!existsSync(join(dir, 'index.html'))) {
		console.log(`  SKIP  ${scene}: no export at ${dir}`);
		continue;
	}
	const r = spawnSync('node', [join(HERE, 'gpu_time.mjs'), '--dir', dir, '--uncapped', '--passes', '--top', '200', '--rounds', '1'], { encoding: 'utf8', timeout: 300000 });
	const out = r.stdout || '';
	if (!/GPU busy/.test(out)) {
		console.log(`  SKIP  ${scene}: gpu_time.mjs measured nothing (exit ${r.status}): ${(out + (r.stderr || '')).trim().split('\n').slice(-2).join(' | ')}`);
		continue;
	}
	ran++;
	const pipelines = new Set();
	for (const line of out.split('\n')) {
		const m = line.match(/^\s+[\d.]+\s+[\d.]+\s+\d+\s+(?:render|compute)\s+(\S.*?)\s+->/);
		if (m) {
			for (const p of m[1].split(',')) {
				pipelines.add(p.trim());
			}
		}
	}
	const offenders = [...pipelines].filter((p) => OPT_IN.test(p));
	if (offenders.length) {
		failed++;
		console.log(`  FAIL  ${scene}: opt-in feature passes run with the feature off: ${offenders.join(', ')}`);
	} else {
		console.log(`  PASS  ${scene}: ${pipelines.size} pipelines, none of an opt-in feature`);
	}
}
if (!ran) {
	console.log('\nRESULT: SKIP');
	process.exit(2);
}
console.log(failed ? '\nRESULT: FAIL' : '\nRESULT: PASS');
process.exit(failed ? 1 : 0);
