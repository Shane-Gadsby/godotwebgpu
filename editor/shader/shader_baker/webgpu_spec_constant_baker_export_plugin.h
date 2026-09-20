/**************************************************************************/
/*  webgpu_spec_constant_baker_export_plugin.h                            */
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

#pragma once

// Task 13 Phase 2 (see webgpu_notes/TASKS.md's 2026-09-20 scoping update):
// reads a Phase 1 browser-recorded specialization-constant usage JSON file
// (see rendering_device_driver_webgpu.cpp's _record_spec_constant_usage()
// doc comment for how that file is produced), installs it for
// RenderingShaderContainerWebGPU::_set_code_from_spirv() to consult while the
// normal shader-baker export plugin (ShaderBakerExportPlugin, upstream/
// shared code -- deliberately NOT modified by this fork for this feature) is
// baking, then collects whatever got baked and writes it as one extra file
// into the export via add_file() -- see drivers/webgpu/spirv_spec_constants.h
// for the accumulator API and the on-disk format this writes.
//
// A completely separate EditorExportPlugin from ShaderBakerExportPlugin
// (rather than a change to it or to ShaderBakerExportPluginPlatformWebGPU)
// deliberately, so this optional, WebGPU-only feature can't affect
// Vulkan/Metal/D3D12 baking at all, and so RenderingShaderContainerWebGPU's
// own baking code path (_set_code_from_spirv(), which every RD backend's
// container class implements) only ever needs the small, generic
// accumulator API in spirv_spec_constants.h, not any knowledge of export
// plugin lifecycles.

#include "editor/export/editor_export_plugin.h"

class WebGPUSpecConstantBakerExportPlugin : public EditorExportPlugin {
	GDCLASS(WebGPUSpecConstantBakerExportPlugin, EditorExportPlugin);

	bool active = false;

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override;

	// NOT _export_end(): that fires from the RAII ExportNotifier's destructor
	// in EditorExportPlatform::ExportNotifier, which lives in the *caller* of
	// EditorExportPlatformWeb::export_project() -- i.e. strictly after that
	// whole function, package-writing included, has already returned. An
	// add_file() call made there is silently too late; the two points that
	// actually flush a plugin's extra_files into the package
	// (add_shared_objects_and_extra_files_from_export_plugins(), called from
	// EditorExportPlatform::export_project_files()) are right after
	// _begin_customize_resources() and right before _end_customize_resources()
	// finishes -- exactly the pattern ShaderBakerExportPlugin itself uses to
	// run its own baking work at the right time. So this plugin uses the same
	// hooks purely for their timing, even though it has no actual resource to
	// customize.
	virtual bool _begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) override;
	virtual void _end_customize_resources() override;

public:
	virtual String get_name() const override { return "WebGPUSpecConstantBaker"; }
};
