/**************************************************************************/
/*  color_temperature.h                                                   */
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

// The engine's one model of the color of a blackbody (a light's correlated color temperature). Light3D, the
// white balance of CameraAttributes and Color.from_temperature() all call it, so a light at T renders neutral under
// a white balance of T. The auto white balance shader (camera_metering.glsl) carries the only other copy of
// planckian_uv(), since a shader cannot call this; keep the two in step.
namespace ColorTemperature {

// The Planckian locus in CIE 1960 UCS (u, v), the rational approximation of Krystek, "An algorithm to calculate
// correlated colour temperature", Color Research & Application 10(1), 1985, as used by Filament
// (https://google.github.io/filament/Filament.md.html#lighting/directlighting/lightsparameterization). Valid for
// 1000-15000 K. Templated so the white balance keeps its double precision and Light3D its float arithmetic: each
// coefficient converts to float exactly as the float literal it used to be, so both stay bit-identical.
template <typename T>
constexpr void planckian_uv(T p_kelvin, T &r_u, T &r_v) {
	const T t2 = p_kelvin * p_kelvin;
	r_u = (T(0.860117757) + T(1.54118254e-4) * p_kelvin + T(1.28641212e-7) * t2) /
			(T(1.0) + T(8.42420235e-4) * p_kelvin + T(7.08145163e-7) * t2);
	r_v = (T(0.317398726) + T(4.22806245e-5) * p_kelvin + T(4.20481691e-8) * t2) /
			(T(1.0) - T(2.89741816e-5) * p_kelvin + T(1.61456053e-7) * t2);
}

// Linear sRGB (Rec. 709 primaries, D65 white) of the locus at the given temperature, with CIE Y = 1. Neither
// normalized nor clamped: channels go negative at the ends of the range. Not clamped to the fit's range either,
// because Light3D never clamped its temperature. The arithmetic (float, the one double reciprocal, real_t vectors)
// is exactly Light3D's since its introduction, so light colors are unchanged.
inline Vector3 planckian_linear_srgb(float p_kelvin) {
	float u, v;
	planckian_uv<float>(p_kelvin, u, v);

	// CIE 1960 uv -> xy.
	const float d = 1.0f / (2.0f * u - 8.0f * v + 4.0f);
	const float x = 3.0f * u * d;
	const float y = 2.0f * v * d;

	// xyY -> XYZ with Y = 1.
	const float a = 1.0 / MAX(y, 1e-5f);
	const Vector3 xyz = Vector3(x * a, 1.0, (1.0f - x - y) * a);

	// XYZ -> linear sRGB (IEC 61966-2-1).
	return Vector3(3.2404542f * xyz.x - 1.5371385f * xyz.y - 0.4985314f * xyz.z,
			-0.9692660f * xyz.x + 1.8760108f * xyz.y + 0.0415560f * xyz.z,
			0.0556434f * xyz.x - 0.2040259f * xyz.y + 1.0572252f * xyz.z);
}

// Applies one of Color.from_temperature()'s normalizations to planckian_linear_srgb()'s result.
inline Color normalize(const Vector3 &p_linear, Color::TemperatureNormalization p_normalization) {
	if (p_normalization == Color::TEMPERATURE_NORMALIZATION_MAX_CHANNEL) {
		// Brightest channel 1, then the negative channel (blue below ~1900 K) clamped: Light3D's normalization
		// with physical light units, operation for operation.
		Vector3 linear = p_linear;
		linear /= MAX(1e-5f, linear[linear.max_axis_index()]);
		return Color(linear.x, linear.y, linear.z).clamp();
	}
	// Rec. 709 luminance 1 (ITU-R BT.709 Y weights) after clamping negative channels, so a change of temperature
	// never changes brightness. Not clamped above: blue exceeds 1 at high temperatures, red at low ones.
	const float r = MAX(float(p_linear.x), 0.0f);
	const float g = MAX(float(p_linear.y), 0.0f);
	const float b = MAX(float(p_linear.z), 0.0f);
	const float luminance = MAX(0.2126f * r + 0.7152f * g + 0.0722f * b, 1e-5f);
	return Color(r / luminance, g / luminance, b / luminance);
}

} // namespace ColorTemperature
