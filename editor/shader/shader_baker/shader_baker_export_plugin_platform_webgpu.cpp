/**************************************************************************/
/*  shader_baker_export_plugin_platform_webgpu.cpp                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "shader_baker_export_plugin_platform_webgpu.h"

#include "drivers/webgpu/rendering_shader_container_webgpu.h"
#include "servers/rendering/rendering_device.h"

// Unlike the Vulkan/Metal/D3D12 platforms, this container format also bakes
// WGSL (via Tint) into every shader it stores — see
// RenderingShaderContainerWebGPU::_set_code_from_spirv() in
// drivers/webgpu/rendering_shader_container_webgpu.cpp. That requires Tint,
// which this file only sees compiled in when the native editor was itself
// built with `webgpu=yes` (WEBGPU_SHADER_BAKER_ENABLED; see
// drivers/webgpu/SCsub and editor_node.cpp's registration of this platform).

RenderingShaderContainerFormat *ShaderBakerExportPluginPlatformWebGPU::create_shader_container_format(const Ref<EditorExportPlatform> &p_platform, const Ref<EditorExportPreset> &p_preset) {
	return memnew(RenderingShaderContainerFormatWebGPU);
}

bool ShaderBakerExportPluginPlatformWebGPU::matches_driver(const String &p_driver) {
	return p_driver == "webgpu";
}

// The capabilities the WebGPU runtime will report, so the baker compiles for the
// browser's device rather than the editor's Vulkan one (webgpu_notes/TASKS.md
// Task 31).
//
// RenderingDeviceDriverWebGPU::has_feature() returns false for every feature
// without exception -- WebGPU 1.0 core exposes none of the optional capabilities
// this enum covers, and its `default:` arm is `return false` so a newly added
// Features entry is false there too until someone deliberately implements it.
// That is why this is a loop over the whole enum rather than a hand-listed table:
// a list would have to be kept in step with a driver this editor cannot even link
// against (drivers/webgpu/ is Emscripten-only and excluded from native builds),
// and would silently go stale the day a feature is added. The invariant to
// preserve is "WebGPU supports no optional features", asserted in one place here.
//
// RenderingDevice::has_feature() answers SUPPORTS_MULTIVIEW and
// SUPPORTS_ATTACHMENT_VRS from capability structs before ever reaching the
// driver, so those two are covered by the same blanket false -- which matches
// the WebGPU driver's own multiview/fragment-shading-rate capabilities, both
// reported unsupported.
void ShaderBakerExportPluginPlatformWebGPU::get_target_feature_overrides(HashMap<int, bool> &r_overrides) const {
	for (int i = 0; i < RenderingDevice::SUPPORTS_MAX; i++) {
		r_overrides[i] = false;
	}
}
