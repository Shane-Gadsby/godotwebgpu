/**************************************************************************/
/*  webgpu_shader_capture_editor_plugin.cpp                               */
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

#include "webgpu_shader_capture_editor_plugin.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_preset.h"
#include "platform/web/export/export_plugin.h"
#include "scene/gui/check_button.h"
#include "scene/main/timer.h"

void WebGPUShaderCaptureEditorPlugin::_update_visibility() {
	bool visible = false;

	// Matches the exact check platform/web/export/export_plugin.cpp's
	// _fix_html() already uses to decide config["renderingDriver"] -- if
	// this project wouldn't get the WebGPU driver in its Web export, this
	// whole feature is meaningless for it.
	String rendering_method = GLOBAL_GET("rendering/renderer/rendering_method.web");
	if (rendering_method == "forward_plus" || rendering_method == "mobile") {
		EditorExport *editor_export = EditorExport::get_singleton();
		if (editor_export) {
			for (int i = 0; i < editor_export->get_export_preset_count(); i++) {
				Ref<EditorExportPreset> preset = editor_export->get_export_preset(i);
				if (preset.is_null()) {
					continue;
				}
				Ref<EditorExportPlatform> platform = preset->get_platform();
				if (platform.is_valid() && platform->get_name() == "Web") {
					visible = true;
					break;
				}
			}
		}
	}

	capture_button->set_visible(visible);
	if (!visible && capture_button->is_pressed()) {
		// Un-arm too -- a button the developer can no longer see shouldn't be
		// left silently affecting future runs if they switch platforms back.
		capture_button->set_pressed(false);
	}
}

void WebGPUShaderCaptureEditorPlugin::_on_toggled(bool p_pressed) {
	EditorExportPlatformWeb::set_capture_spec_constants_enabled(p_pressed);
	capture_button->set_text(p_pressed ? TTR("Capturing Shaders...") : TTR("Capture Shaders"));
}

WebGPUShaderCaptureEditorPlugin::WebGPUShaderCaptureEditorPlugin() {
	capture_button = memnew(CheckButton);
	capture_button->set_text(TTR("Capture Shaders"));
	capture_button->set_tooltip_text(TTR(
			"While enabled, the next Run in Browser session(s) will automatically stream "
			"specialization-constant shader usage back to the editor for export-time baking "
			"(see webgpu_notes/TASKS.md Task 13). Equivalent to manually setting "
			"window.GODOT_WEBGPU_RECORD_SPEC_CONSTANTS = true in the browser console, but "
			"without needing devtools or a manual JSON download."));
	capture_button->set_visible(false);
	capture_button->connect(SceneStringName(toggled), callable_mp(this, &WebGPUShaderCaptureEditorPlugin::_on_toggled));
	add_control_to_container(CONTAINER_TOOLBAR, capture_button);

	// No signal fires when export presets or the project's web renderer
	// setting change (checked during scoping -- see this class's header doc
	// comment), so visibility is polled on a low-frequency timer instead.
	visibility_timer = memnew(Timer);
	visibility_timer->set_wait_time(1.0);
	visibility_timer->set_autostart(true);
	visibility_timer->connect("timeout", callable_mp(this, &WebGPUShaderCaptureEditorPlugin::_update_visibility));
	capture_button->add_child(visibility_timer);

	debugger_plugin.instantiate();
	add_debugger_plugin(debugger_plugin);

	_update_visibility();
}

WebGPUShaderCaptureEditorPlugin::~WebGPUShaderCaptureEditorPlugin() {
	// Don't leave a future run silently affected by a toggle whose button no
	// longer exists.
	EditorExportPlatformWeb::set_capture_spec_constants_enabled(false);
	if (debugger_plugin.is_valid()) {
		remove_debugger_plugin(debugger_plugin);
	}
}
