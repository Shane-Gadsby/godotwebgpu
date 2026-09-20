/**************************************************************************/
/*  webgpu_spec_constant_debugger_plugin.h                                */
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
// scoping note): receives "webgpu:spec_constant_usage" debugger messages
// sent by _record_spec_constant_usage() (drivers/webgpu/rendering_device_driver_webgpu.cpp)
// over the same remote-debugger connection Run-in-Browser already opens
// (see WebGPUShaderCaptureEditorPlugin, which arms the running instance to
// send these in the first place), and writes each one straight into
// `res://.godot/webgpu_spec_constant_usage_captured.json` -- replacing the
// manual "devtools console -> download JSON -> repoint export option"
// workflow with "toggle Capture Shaders, play, stop."
//
// Deliberately does not try to precisely deduplicate against what's already
// in the file across separate capture sessions (the sending side already
// dedupes within one session, via _spec_constant_recording_seen) -- a rare
// cross-session duplicate entry just means one wasted bake attempt at
// export time, not an incorrect one. Accumulates across as many Play
// sessions as the developer runs with the toggle on, rather than resetting
// per-session, matching that toggle's persistent (not one-shot) design.

#include "editor/debugger/editor_debugger_plugin.h"

class WebGPUSpecConstantDebuggerPlugin : public EditorDebuggerPlugin {
	GDCLASS(WebGPUSpecConstantDebuggerPlugin, EditorDebuggerPlugin);

public:
	virtual bool has_capture(const String &p_capture) const override;
	virtual bool capture(const String &p_message, const Array &p_data, int p_session) override;
};
