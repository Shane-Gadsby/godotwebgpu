/**************************************************************************/
/*  webgpu_shader_capture_editor_plugin.h                                 */
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

// Task 13 Phase 4 (webgpu_notes/TASKS.md's 2026-09-20 streaming-capture
// scoping note): a small toolbar toggle, "Capture Shaders", visible only
// when the current project targets Web with the WebGPU renderer and has at
// least one Web export preset. While toggled on, it arms
// EditorExportPlatformWeb::capture_spec_constants_enabled
// (platform/web/export/export_plugin.h), which makes every subsequent
// Run-in-Browser session (via the editor's existing EditorRunNative
// remote-deploy dropdown -- this button doesn't launch anything itself, it
// only arms the flag the *existing* run path already reads) pass
// `--webgpu-record-spec-constants` to the running instance, which in turn
// makes it stream recorded specialization-constant usage back over the same
// remote-debugger WebSocket connection Run-in-Browser already opens (see
// WebGPUSpecConstantDebuggerPlugin), instead of the manual devtools-console
// workflow.
//
// Deliberately its own small EditorPlugin, not a change to any Web export
// UI, following the precedent in modules/openxr/editor/openxr_editor_plugin.cpp
// (a project-condition-gated CONTAINER_TOOLBAR control).

#include "editor/plugins/editor_plugin.h"
#include "editor/shader/shader_baker/webgpu_spec_constant_debugger_plugin.h"

class CheckButton;
class Timer;

class WebGPUShaderCaptureEditorPlugin : public EditorPlugin {
	GDCLASS(WebGPUShaderCaptureEditorPlugin, EditorPlugin);

	CheckButton *capture_button = nullptr;
	Timer *visibility_timer = nullptr;
	Ref<WebGPUSpecConstantDebuggerPlugin> debugger_plugin;

	// No signal exists for "the set of export presets changed" or "the
	// project's web renderer setting changed" (checked during scoping) --
	// polled on a low-frequency timer instead. Cheap: a handful of Ref
	// lookups roughly once a second, only while the editor is open.
	void _update_visibility();
	void _on_toggled(bool p_pressed);

public:
	virtual String get_plugin_name() const override { return "WebGPUShaderCapture"; }

	WebGPUShaderCaptureEditorPlugin();
	~WebGPUShaderCaptureEditorPlugin();
};
