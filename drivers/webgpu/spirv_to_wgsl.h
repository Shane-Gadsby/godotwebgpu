/**************************************************************************/
/*  spirv_to_wgsl.h                                                       */
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

#include <cstdint>

// Shared SPIR-V → WGSL translation entry point (spirv_preprocess::run_pipeline()
// + SPIRV-Cross's WGSL backend). Used by the runtime driver for any shader
// that was not precompiled or baked (rendering_device_driver_webgpu.cpp,
// WEBGPU_ENABLED, web builds); the export-time shader baker runs the same
// pipeline out of process, through bin/wgsl_convert_cli
// (wgsl_bake_subprocess.cpp), so baked containers embed the exact same WGSL
// a browser would otherwise produce at runtime.

namespace webgpu {

// Returns a malloc'd null-terminated WGSL string (caller must free), or
// nullptr on failure (an error is already printed via ERR_PRINT).
char *spirv_to_wgsl(const uint8_t *p_spv_ptr, int p_spv_size);

} // namespace webgpu
