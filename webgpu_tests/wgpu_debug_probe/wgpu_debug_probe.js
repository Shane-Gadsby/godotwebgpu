// wgpu_debug_probe.js
//
// Standalone WebGPU call-site probe for tracking down real-hardware-only
// validation errors that this project's sandbox can't reproduce (see
// webgpu_notes/TASKS.md Task 9.5 Round 9/10 -- the still-unidentified
// "TextureViewDimension ... not compatible with ... TextureDimension" error).
//
// WHY THIS HAS TO BE A SEPARATELY-LOADED SCRIPT, NOT A CONSOLE SNIPPET:
// GPUDevice/GPUTexture are ordinary JS prototypes. Patching them only works
// if the patch runs *before* the page's own script creates the device and
// textures -- pasting code into DevTools console after the page has already
// loaded is too late (the engine creates its device almost immediately).
// There's no way to inject a console snippet "before page load" without
// DevTools' Local Overrides feature or a browser extension, so the simplest
// reliable approach is: add this as the FIRST <script> tag in the exported
// index.html, before Godot's own engine script. See the README next to this
// file for the exact one-line edit.
//
// USAGE:
//   1. Copy this file next to your exported index.html.
//   2. Add <script src="wgpu_debug_probe.js"></script> as the very first
//      line inside <head>, before any other <script> tag.
//   3. Reload the page and let the scene run as normal (through
//      "Rendering 10 frames..." and beyond -- leave it running an extra
//      10-20s past that to be safe).
//   4. In DevTools console, run:
//        window.__wgpuProbeDump()        // human-readable summary
//        copy(window.__wgpuProbeExport()) // copies full JSON log to clipboard
//      and paste the output back.
//
// WHAT IT DOES:
//   Wraps GPUDevice.prototype.createTexture and GPUTexture.prototype.createView
//   so every call is logged with its real arguments (size/format/dimension/
//   usage/label) and a JS stack trace, *before* the browser's own async
//   validation ever runs -- so even if Dawn's error message is vague or the
//   texture has no debug label ("unnamed#N"), this probe has the ground truth
//   captured directly from the call site. It specifically flags any
//   createView() call whose requested view dimension is structurally
//   incompatible with the real texture's own dimension (the same check this
//   driver's C++ side already applies as a *fallback*, Task 9.5 Round 6/8) --
//   that's the exact condition that produces the "not compatible with the
//   dimension" validation error.

(function () {
	'use strict';

	if (window.__wgpuProbeInstalled) {
		return;
	}
	window.__wgpuProbeInstalled = true;

	const log = [];
	let nextId = 1;
	const t0 = performance.now();

	function shortStack(skip) {
		const raw = new Error().stack || '';
		const lines = raw.split('\n').slice(skip);
		// Keep it readable: cap at 12 frames, strip the leading "Error" line noise.
		return lines.slice(0, 12).join('\n');
	}

	function extentToXYZ(size) {
		// GPUExtent3D is either [w, h, d] or {width, height, depthOrArrayLayers}.
		if (Array.isArray(size)) {
			return { width: size[0] | 0, height: (size[1] === undefined ? 1 : size[1]) | 0, depth: (size[2] === undefined ? 1 : size[2]) | 0 };
		}
		if (size && typeof size === 'object') {
			return {
				width: (size.width || 0) | 0,
				height: (size.height === undefined ? 1 : size.height) | 0,
				depth: (size.depthOrArrayLayers === undefined ? 1 : size.depthOrArrayLayers) | 0,
			};
		}
		return { width: 0, height: 0, depth: 0 };
	}

	// dimension-class compatibility, mirroring the driver's own C++ check
	// (rendering_device_driver_webgpu.cpp, dim_class_incompatible /
	// swt_dim_class_incompatible, Task 9.5 Round 6):
	//   texture dimension "3d"      -> only a "3d" view is legal
	//   texture dimension "1d"      -> only a "1d" view is legal
	//   texture dimension "2d"      -> "2d"/"2d-array"/"cube"/"cube-array" legal, never "1d"/"3d"
	function viewDimCompatible(texDim, viewDim) {
		const td = texDim || '2d';
		const vd = viewDim || '2d'; // WebGPU defaults an omitted viewDimension based on the texture; '2d' is the common case.
		if (td === '3d') {
			return vd === '3d';
		}
		if (td === '1d') {
			return vd === '1d';
		}
		return vd !== '1d' && vd !== '3d';
	}

	// WeakMap from GPUTexture instance -> the descriptor it was created with,
	// so createView() can look up the real dimension/size/format/label even
	// though GPUTexture itself doesn't expose its creation-time dimension.
	const textureInfo = new WeakMap();

	const origCreateTexture = GPUDevice.prototype.createTexture;
	GPUDevice.prototype.createTexture = function (descriptor) {
		const id = nextId++;
		const xyz = extentToXYZ(descriptor && descriptor.size);
		const dim = (descriptor && descriptor.dimension) || '2d';
		const info = {
			id,
			t: +(performance.now() - t0).toFixed(1),
			format: descriptor && descriptor.format,
			dimension: dim,
			size: xyz,
			mipLevelCount: (descriptor && descriptor.mipLevelCount) || 1,
			sampleCount: (descriptor && descriptor.sampleCount) || 1,
			usage: descriptor && descriptor.usage,
			label: (descriptor && descriptor.label) || null,
		};
		const isTargetProfile = dim === '3d' && xyz.depth === 1 && xyz.width === 4 && xyz.height === 4;
		const entry = {
			kind: 'createTexture',
			...info,
			isTargetProfile,
			stack: shortStack(2),
		};
		log.push(entry);
		if (isTargetProfile) {
			console.log('%c[WGPU-PROBE] createTexture MATCHES target profile (3d, 4x4x1) #' + id, 'color:#e91e63;font-weight:bold', entry);
		} else if (dim === '3d') {
			console.log('[WGPU-PROBE] createTexture (3d, other size) #' + id, entry);
		}
		const tex = origCreateTexture.call(this, descriptor);
		textureInfo.set(tex, info);
		return tex;
	};

	const origCreateView = GPUTexture.prototype.createView;
	GPUTexture.prototype.createView = function (descriptor) {
		const info = textureInfo.get(this) || null;
		const viewDim = descriptor && descriptor.dimension;
		const compatible = info ? viewDimCompatible(info.dimension, viewDim) : null;
		const entry = {
			kind: 'createView',
			t: +(performance.now() - t0).toFixed(1),
			textureId: info ? info.id : null,
			textureDimension: info ? info.dimension : '(unknown -- texture created before probe installed)',
			textureSize: info ? info.size : null,
			textureFormat: info ? info.format : null,
			textureLabel: info ? info.label : null,
			requestedViewDimension: viewDim || '(default)',
			compatible,
			stack: shortStack(2),
		};
		log.push(entry);
		if (compatible === false) {
			console.log('%c[WGPU-PROBE] INCOMPATIBLE createView -- this is the crash! texture #' + entry.textureId, 'color:#e91e63;font-weight:bold;font-size:1.1em', entry);
		}
		return origCreateView.call(this, descriptor);
	};

	window.__wgpuProbeLog = log;

	window.__wgpuProbeDump = function () {
		const textures3d = log.filter((e) => e.kind === 'createTexture' && e.dimension === '3d');
		const badViews = log.filter((e) => e.kind === 'createView' && e.compatible === false);
		console.log('=== WGPU Probe summary ===');
		console.log(textures3d.length + ' 3D texture(s) created:');
		for (const e of textures3d) {
			console.log('  #' + e.id + ' t=' + e.t + 'ms ' + e.size.width + 'x' + e.size.height + 'x' + e.size.depth +
				' mips=' + e.mipLevelCount + ' fmt=' + e.format + ' label=' + (e.label || '(none)') +
				(e.isTargetProfile ? '  <-- MATCHES target profile' : ''));
		}
		console.log(badViews.length + ' incompatible createView() call(s):');
		for (const e of badViews) {
			console.log('  texture #' + e.textureId + ' (' + e.textureDimension + ', ' +
				(e.textureSize ? e.textureSize.width + 'x' + e.textureSize.height + 'x' + e.textureSize.depth : '?') +
				', ' + e.textureFormat + ', label=' + (e.textureLabel || '(none)') +
				') asked for view dimension "' + e.requestedViewDimension + '" at t=' + e.t + 'ms');
			console.log('    stack:\n' + e.stack.split('\n').map((l) => '      ' + l).join('\n'));
		}
		if (badViews.length === 0) {
			console.log('  (none seen yet -- let the scene run longer, or the failing call may happen after this dump)');
		}
		return { textures3d, badViews };
	};

	window.__wgpuProbeExport = function () {
		return JSON.stringify(log, null, 2);
	};

	console.log('[WGPU-PROBE] installed -- GPUDevice.createTexture / GPUTexture.createView are now logged.');
	console.log('[WGPU-PROBE] run window.__wgpuProbeDump() any time, or copy(window.__wgpuProbeExport()) for the full log.');
})();
