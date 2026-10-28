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

#include "core/math/color.h"
#include "core/math/vector3.h"
#include "core/templates/vector.h"

// Environment color grading: display-referred controls (after the tonemapper, on the sRGB-encoded image, where the
// Environment's brightness/contrast/saturation already work) baked on the CPU into one 3D LUT, which the renderers
// apply through the color correction slot. The model, step by step, is in color_grading.cpp.
class RendererColorGrading {
public:
	// Curves come from the scene as CURVE_SAMPLES evenly spaced samples over x = 0..1; an empty array is no curve.
	static constexpr int CURVE_SAMPLES = 256;
	// Lattice points per axis: 33 is what grading LUTs use (URP's default LUT size is 32, Resolve and the .cube
	// format's common sizes are 33 and 65); a 33^3 RGBA16F texture is 288 KB.
	static constexpr int LUT_SIZE = 33;

	enum Curve {
		CURVE_MASTER,
		CURVE_RED,
		CURVE_GREEN,
		CURVE_BLUE,
		CURVE_HUE_VS_HUE,
		CURVE_HUE_VS_SAT,
		CURVE_SAT_VS_SAT,
		CURVE_LUM_VS_SAT,
		CURVE_MAX,
	};

	struct Params {
		bool enabled = false;

		Color color_filter = Color(1, 1, 1);
		float hue_shift = 0.0; // Degrees.
		float vibrance = 0.0;

		Color lift = Color(0, 0, 0);
		Color gamma = Color(1, 1, 1);
		Color gain = Color(1, 1, 1);

		Color shadows = Color(1, 1, 1);
		Color midtones = Color(1, 1, 1);
		Color highlights = Color(1, 1, 1);
		float shadows_start = 0.0;
		float shadows_end = 0.3;
		float highlights_start = 0.55;
		float highlights_end = 1.0;

		Vector3 mixer_red = Vector3(1, 0, 0);
		Vector3 mixer_green = Vector3(0, 1, 0);
		Vector3 mixer_blue = Vector3(0, 0, 1);

		Color split_shadows = Color(0.5, 0.5, 0.5);
		Color split_highlights = Color(0.5, 0.5, 0.5);
		float split_balance = 0.0;

		Vector<float> curves[CURVE_MAX];
	};

	// The grade of one sRGB-encoded color (components 0..1), sRGB-encoded and clamped to 0..1.
	static Color grade(const Params &p_params, const Color &p_srgb);

	// The LUT: LUT_SIZE^3 RGBA texels as half floats, red fastest, then green, then blue (a 3D texture's layout).
	// Texel (i, j, k) holds the grade of the input (i, j, k) / (LUT_SIZE - 1): lattice points, not texel centers,
	// so it is sampled at coord * (LUT_SIZE - 1) / LUT_SIZE + 0.5 / LUT_SIZE.
	static Vector<uint8_t> bake(const Params &p_params);
};
