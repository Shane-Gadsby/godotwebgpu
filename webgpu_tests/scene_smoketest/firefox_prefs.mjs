// Firefox prefs for a real-GPU WebGPU run on Linux desktop (the Firefox target, user 2026-10-25): WebGPU on, its
// blocklist and WebRender's ignored, and wgpu pinned to its Vulkan backend (dom.webgpu.wgpu-backend, read by Firefox's
// WebGPUParent) so a run never lands on another backend silently. gfx.webgpu.force-enabled is what the older runners set.
export const FIREFOX_WEBGPU_PREFS = {
	'dom.webgpu.enabled': true,
	'gfx.webgpu.force-enabled': true,
	'gfx.webgpu.ignore-blocklist': true,
	'gfx.webrender.all': true,
	'dom.webgpu.wgpu-backend': 'vulkan',
};
