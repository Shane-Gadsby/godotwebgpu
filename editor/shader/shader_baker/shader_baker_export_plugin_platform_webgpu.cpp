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
#include "drivers/webgpu/wgsl_bake_subprocess.h"
#include "servers/rendering/rendering_device.h"

// Unlike the Vulkan/Metal/D3D12 platforms, this container format also bakes
// WGSL (via bin/wgsl_convert_cli) into every shader it stores — see
// RenderingShaderContainerWebGPU::_set_code_from_spirv() in
// drivers/webgpu/rendering_shader_container_webgpu.cpp. That requires the
// baker half of drivers/webgpu, which this file only sees compiled in when the native editor was itself
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

// The WGSL bake, batched: every stage of every variant goes through a few
// concurrent wgsl_convert_cli runs before any container is built, and the
// containers then find their WGSL ready (webgpu::bake_wgsl_via_subprocess()
// reads the batch's results first). See WgslBakeBatch for why.
class ShaderBakerStageBatchWebGPU : public ShaderBakerStageBatch {
	webgpu::WgslBakeBatch batch;

public:
	virtual void add_stage(const Vector<uint8_t> &p_spirv) override { batch.add_stage(p_spirv); }
	virtual void start() override { batch.start(); }
	virtual bool is_done() const override { return batch.is_done(); }
	virtual uint32_t get_done_count() const override { return batch.get_done_count(); }
	virtual uint32_t get_total_count() const override { return batch.get_total_count(); }
	virtual void cancel() override { batch.cancel(); }
	virtual void finish() override { batch.finish(); }
};

ShaderBakerStageBatch *ShaderBakerExportPluginPlatformWebGPU::create_stage_batch() {
	return memnew(ShaderBakerStageBatchWebGPU);
}

// The web exporter writes the baked cache to <name>.shaders.pck when the
// preset asks for it (EditorExportPlatformWeb::_save_shader_pack()). Not with
// an encrypted main pack: the side pack is written unencrypted, and silently
// moving files out of an encrypted pack is not a choice to make for the user.
bool ShaderBakerExportPluginPlatformWebGPU::uses_separate_pack(const Ref<EditorExportPreset> &p_preset) const {
	if (p_preset.is_null() || p_preset->get_enc_pck()) {
		return false;
	}
	return bool(p_preset->get("shader_baker/separate_pack"));
}

// shader_baker/variant_pruning (0 Disabled, 1 Automatic): leave out the shader
// groups this export cannot select. See ShaderRD::GroupBakeRequirement and
// ShaderBakerExportPlugin::_compute_unmet_group_requirements().
bool ShaderBakerExportPluginPlatformWebGPU::uses_variant_pruning(const Ref<EditorExportPreset> &p_preset) const {
	if (p_preset.is_null() || !p_preset->has("shader_baker/variant_pruning")) {
		return false;
	}
	return int(p_preset->get("shader_baker/variant_pruning")) != 0;
}

// The web shell can fetch a file after the game has started, which is all the
// extras need; they go next to the separate shader pack, so only with one.
bool ShaderBakerExportPluginPlatformWebGPU::uses_lazy_shader_extras(const Ref<EditorExportPreset> &p_preset) const {
	return uses_variant_pruning(p_preset) && uses_separate_pack(p_preset);
}
