/**************************************************************************/
/*  wgsl_bake_subprocess.h                                                */
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

// Native-editor-only (WEBGPU_SHADER_BAKER_ENABLED): bakes SPIR-V -> WGSL for
// export-time shader baking by shelling out to bin/tint_convert_cli, one
// subprocess per shader stage, instead of calling Tint in-process.
//
// This is a deliberate, load-bearing design choice, not a simplification:
// Tint can hit an internal-compiler-error assert() on a shader variant it
// doesn't handle and abort() the whole process. Export-time baking compiles
// every declared shader variant (not just ones a game actually exercises at
// runtime), so it hits this far more often than the runtime Tint fallback
// ever does. tint_convert_cli running as a separate OS process means that
// abort() only kills the subprocess -- the editor sees a non-zero/abnormal
// exit and simply leaves that one stage unbaked (get_wgsl_code() returns
// nullptr for it, and the runtime driver's existing Tint fallback still
// handles it in the browser, exactly as if it had never been baked).
//
// Never linking Tint into the editor at all also means this file (and the
// container class that calls it) never need spirv_preprocess.cpp,
// tint_wrapper.cpp, or the vendored Tint/SPIRV-Tools sources compiled for
// linuxbsd/macos/windows -- see drivers/webgpu/SCsub.

#include "core/string/ustring.h"

namespace webgpu {

// Runs bin/tint_convert_cli (located next to the running editor executable)
// on p_spv_bytes and returns the resulting WGSL, or an empty string if the
// tool is missing, the subprocess exits abnormally (including a Tint
// internal-compiler-error abort), or conversion otherwise fails. Safe to
// call concurrently from multiple WorkerThreadPool threads (shader baking
// runs in parallel; see ShaderBakerExportPlugin::_customize_shader_version()).
String bake_wgsl_via_subprocess(const uint8_t *p_spv_ptr, int p_spv_size);

} // namespace webgpu
