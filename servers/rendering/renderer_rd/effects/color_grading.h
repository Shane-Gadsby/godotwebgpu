/**************************************************************************/
/*  color_grading.h                                                       */
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

#include "core/templates/hash_map.h"
#include "servers/rendering/renderer_rd/shaders/effects/color_grading.glsl.gen.h"

namespace RendererRD {

// The Environment's color grading for the tonemap pass: a 33^3 3D LUT in the color correction slot (no new binding
// in the tonemap shaders). The CPU bakes the grade (RendererColorGrading) when a grading property changes; a small
// compute pass composes it with the Environment's own color correction LUT, when one is set, and decodes to linear
// for viewports that tonemap to linear (HDR). Both run only when their inputs change.
class ColorGrading {
public:
	ColorGrading();
	~ColorGrading();

	// The LUT for p_environment's grading, or an invalid RID when grading is off. p_user_lut is the color correction
	// texture exactly as the tonemap pass would bind it (invalid for none), p_linear_output true when the tonemap pass
	// does not convert to sRGB. With p_update false nothing is baked or dispatched (inside a render pass), and a LUT
	// that is not up to date is not returned.
	RID get_lut(RID p_environment, RID p_user_lut, bool p_user_lut_1d, bool p_linear_output, bool p_update);

private:
	enum Mode {
		MODE_USER_LUT_3D,
		MODE_USER_LUT_1D,
		MODE_MAX
	};

	enum {
		FLAG_USE_USER_LUT = 1,
		FLAG_LINEARIZE = 2,
	};

	struct PushConstant {
		uint32_t flags;
		uint32_t size;
		uint32_t pad[2];
	};

	struct Output {
		RID texture;
		uint64_t version = 0;
		RID user_lut;
		bool user_lut_1d = false;
	};

	struct Entry {
		uint64_t version = 0;
		RID grading; // The CPU bake, RGBA16F.
		Output outputs[2]; // [0] sRGB-encoded output, [1] linear output.
	};

	ColorGradingShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];

	HashMap<RID, Entry> entries;

	static void _free_entry(Entry &r_entry);
	RID _create_lut_texture(uint32_t p_usage, const Vector<uint8_t> &p_data);
};

} // namespace RendererRD
