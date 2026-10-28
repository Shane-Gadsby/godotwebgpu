/**************************************************************************/
/*  color_grading.cpp                                                     */
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

#include "color_grading.h"

#include "core/math/math_funcs.h"

// The model. Every step works on the image the tonemapper produced (display-referred), in this order, the order of
// URP's LDR grading LUT (LutBuilderLdr.shader, Unity 6), which Unity, Unreal and Resolve users expect: filters and
// mixing in linear light, then the tonal and creative controls on the sRGB-encoded values, the way an editor's
// color page works on a display-referred image.
//  1. Decode sRGB to linear (IEC 61966-2-1).
//  2. Color filter: multiply by the filter color (linear). URP Color Adjustments "Color Filter".
//  3. Channel mixer: each output channel is a weighted sum of the input channels. URP Channel Mixer.
//  4. Shadows/midtones/highlights: three multipliers weighted by the pixel's Rec. 709 luminance, shadows fading out
//     over [shadows_start, shadows_end] and highlights in over [highlights_start, highlights_end] with smoothstep,
//     midtones the rest. URP Shadows Midtones Highlights (same default ranges).
//  5. Encode to sRGB (negative light from the mixer clipped to 0).
//  6. Lift/gamma/gain per channel: v = gain * (x + lift * (1 - x)), then v^(1/gamma). The classic three-way
//     corrector: lift moves black and keeps white, gain scales and keeps black, gamma bends the midtones (Blender's
//     Color Balance node in Lift/Gamma/Gain mode, ASC CDL-like but anchored at both ends).
//  7. Split toning: soft-light the shadows toward one color and the highlights toward another, balanced on luma. URP
//     Split Toning, which does it on gamma-encoded values to match Adobe's tools, as here.
//  8. Hue and saturation: hue shift and the hue-vs-hue curve rotate the hue (HSV); the hue-vs-sat, sat-vs-sat and
//     lum-vs-sat curves scale saturation around luma (URP Color Curves, whose curves are neutral at 0.5 as here: hue
//     offset = (y - 0.5) turns, saturation multiplier = 2 y; every curve reads the color before this step); then
//     vibrance raises the saturation of the least saturated colors most (the formula of CeeJay.dk's Vibrance.fx, as
//     ReShade ships it).
//  9. Curves: master on every channel, then red, green and blue (URP's YRGB curves; Photoshop/Resolve curves).
// 10. Clamp to 0..1.
// Luma is Rec. 709 (ITU-R BT.709-6) everywhere, on linear values in step 4 and encoded values in 7 and 8.

static const float LUMA_R = 0.2126f;
static const float LUMA_G = 0.7152f;
static const float LUMA_B = 0.0722f;

static float _srgb_to_linear(float p_v) {
	return p_v <= 0.04045f ? p_v * (1.0f / 12.92f) : Math::pow((p_v + 0.055f) * (1.0f / 1.055f), 2.4f);
}

static float _linear_to_srgb(float p_v) {
	p_v = MAX(p_v, 0.0f);
	return p_v <= 0.0031308f ? p_v * 12.92f : 1.055f * Math::pow(p_v, 1.0f / 2.4f) - 0.055f;
}

static float _smoothstep(float p_from, float p_to, float p_x) {
	if (p_to <= p_from) {
		return p_x < p_from ? 0.0f : 1.0f;
	}
	const float t = CLAMP((p_x - p_from) / (p_to - p_from), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// URP's SoftLight() (Color.hlsl), the W3C/Pegtop-style formula: blend 0.5 leaves the base unchanged.
static float _soft_light(float p_base, float p_blend) {
	p_base = MAX(p_base, 0.0f);
	if (p_blend < 0.5f) {
		return 2.0f * p_base * p_blend + p_base * p_base * (1.0f - 2.0f * p_blend);
	}
	return Math::sqrt(p_base) * (2.0f * p_blend - 1.0f) + 2.0f * p_base * (1.0f - p_blend);
}

// A sampled curve at x (0..1), linearly interpolated between samples.
static float _sample_curve(const Vector<float> &p_curve, float p_x) {
	const int n = p_curve.size();
	if (n == 1) {
		return p_curve[0];
	}
	const float x = CLAMP(p_x, 0.0f, 1.0f) * float(n - 1);
	const int i = MIN(int(x), n - 2);
	return Math::lerp(p_curve[i], p_curve[i + 1], x - float(i));
}

static float _luma(float p_r, float p_g, float p_b) {
	return LUMA_R * p_r + LUMA_G * p_g + LUMA_B * p_b;
}

// The parameters' colors in linear light, computed once per bake rather than per lattice point.
struct ColorGradingLinear {
	float filter[3];
	float shadows[3];
	float midtones[3];
	float highlights[3];

	explicit ColorGradingLinear(const RendererColorGrading::Params &p_params) {
		const Color *colors[4] = { &p_params.color_filter, &p_params.shadows, &p_params.midtones, &p_params.highlights };
		float *dest[4] = { filter, shadows, midtones, highlights };
		for (int i = 0; i < 4; i++) {
			dest[i][0] = _srgb_to_linear(colors[i]->r);
			dest[i][1] = _srgb_to_linear(colors[i]->g);
			dest[i][2] = _srgb_to_linear(colors[i]->b);
		}
	}
};

static Color _grade(const RendererColorGrading::Params &p_params, const ColorGradingLinear &p_linear, const Color &p_srgb) {
	using CG = RendererColorGrading;
	const CG::Params &p = p_params;
	const ColorGradingLinear &pl = p_linear;
	float r = _srgb_to_linear(p_srgb.r);
	float g = _srgb_to_linear(p_srgb.g);
	float b = _srgb_to_linear(p_srgb.b);

	// 2. Color filter. Colors are sRGB-encoded like every Color in the inspector: the multiplier is the linear value.
	r *= pl.filter[0];
	g *= pl.filter[1];
	b *= pl.filter[2];

	// 3. Channel mixer.
	{
		const float mr = p.mixer_red.x * r + p.mixer_red.y * g + p.mixer_red.z * b;
		const float mg = p.mixer_green.x * r + p.mixer_green.y * g + p.mixer_green.z * b;
		const float mb = p.mixer_blue.x * r + p.mixer_blue.y * g + p.mixer_blue.z * b;
		r = mr;
		g = mg;
		b = mb;
	}

	// 4. Shadows/midtones/highlights. The colors are tints that multiply (white is neutral), linear like the filter.
	{
		const float luma = _luma(r, g, b);
		const float fs = 1.0f - _smoothstep(p.shadows_start, p.shadows_end, luma);
		const float fh = _smoothstep(p.highlights_start, p.highlights_end, luma);
		const float fm = 1.0f - fs - fh;
		r *= pl.shadows[0] * fs + pl.midtones[0] * fm + pl.highlights[0] * fh;
		g *= pl.shadows[1] * fs + pl.midtones[1] * fm + pl.highlights[1] * fh;
		b *= pl.shadows[2] * fs + pl.midtones[2] * fm + pl.highlights[2] * fh;
	}

	// 5. Back to the encoded values.
	float c[3] = { _linear_to_srgb(r), _linear_to_srgb(g), _linear_to_srgb(b) };

	// 6. Lift/gamma/gain. Their values are amounts, used as they are (not linearized).
	const float lift[3] = { p.lift.r, p.lift.g, p.lift.b };
	const float gamma[3] = { p.gamma.r, p.gamma.g, p.gamma.b };
	const float gain[3] = { p.gain.r, p.gain.g, p.gain.b };
	for (int i = 0; i < 3; i++) {
		float v = gain[i] * (c[i] + lift[i] * (1.0f - c[i]));
		v = MAX(v, 0.0f);
		if (gamma[i] != 1.0f) {
			v = Math::pow(v, 1.0f / MAX(gamma[i], 0.001f));
		}
		c[i] = v;
	}

	// 7. Split toning. Gray (0.5) is neutral; balance -1..1 moves the split toward the highlights or the shadows.
	if (p.split_shadows != Color(0.5, 0.5, 0.5) || p.split_highlights != Color(0.5, 0.5, 0.5)) {
		const float t = CLAMP(_luma(CLAMP(c[0], 0.0f, 1.0f), CLAMP(c[1], 0.0f, 1.0f), CLAMP(c[2], 0.0f, 1.0f)) + p.split_balance, 0.0f, 1.0f);
		const float sh[3] = { p.split_shadows.r, p.split_shadows.g, p.split_shadows.b };
		const float hi[3] = { p.split_highlights.r, p.split_highlights.g, p.split_highlights.b };
		for (int i = 0; i < 3; i++) {
			c[i] = _soft_light(c[i], Math::lerp(0.5f, sh[i], 1.0f - t));
			c[i] = _soft_light(c[i], Math::lerp(0.5f, hi[i], t));
		}
	}

	// 8. Hue and saturation.
	const bool use_hue_vs_hue = !p.curves[CG::CURVE_HUE_VS_HUE].is_empty();
	const bool use_hue_vs_sat = !p.curves[CG::CURVE_HUE_VS_SAT].is_empty();
	const bool use_sat_vs_sat = !p.curves[CG::CURVE_SAT_VS_SAT].is_empty();
	const bool use_lum_vs_sat = !p.curves[CG::CURVE_LUM_VS_SAT].is_empty();
	if (p.hue_shift != 0.0f || use_hue_vs_hue || use_hue_vs_sat || use_sat_vs_sat || use_lum_vs_sat) {
		// HSV of the clamped color (hue and saturation are only defined there).
		Color hsv_color(CLAMP(c[0], 0.0f, 1.0f), CLAMP(c[1], 0.0f, 1.0f), CLAMP(c[2], 0.0f, 1.0f));
		const float hue = hsv_color.get_h();
		const float sat = hsv_color.get_s();
		const float lum = _luma(c[0], c[1], c[2]);

		float new_hue = hue + p.hue_shift / 360.0f;
		if (use_hue_vs_hue) {
			new_hue += _sample_curve(p.curves[CG::CURVE_HUE_VS_HUE], hue) - 0.5f;
		}
		if (new_hue != hue) {
			new_hue -= Math::floor(new_hue);
			hsv_color.set_hsv(new_hue, sat, hsv_color.get_v());
			c[0] = hsv_color.r;
			c[1] = hsv_color.g;
			c[2] = hsv_color.b;
		}

		float sat_mult = 1.0f;
		if (use_hue_vs_sat) {
			sat_mult *= 2.0f * _sample_curve(p.curves[CG::CURVE_HUE_VS_SAT], hue);
		}
		if (use_sat_vs_sat) {
			sat_mult *= 2.0f * _sample_curve(p.curves[CG::CURVE_SAT_VS_SAT], sat);
		}
		if (use_lum_vs_sat) {
			sat_mult *= 2.0f * _sample_curve(p.curves[CG::CURVE_LUM_VS_SAT], lum);
		}
		if (sat_mult != 1.0f) {
			const float l = _luma(c[0], c[1], c[2]);
			for (int i = 0; i < 3; i++) {
				c[i] = l + sat_mult * (c[i] - l);
			}
		}
	}
	if (p.vibrance != 0.0f) {
		// Vibrance.fx: mix(luma, color, 1 + vibrance * (1 - sign(vibrance) * saturation)), saturation = max - min.
		const float l = _luma(c[0], c[1], c[2]);
		const float saturation = MAX(c[0], MAX(c[1], c[2])) - MIN(c[0], MIN(c[1], c[2]));
		const float amount = 1.0f + p.vibrance * (1.0f - SIGN(p.vibrance) * saturation);
		for (int i = 0; i < 3; i++) {
			c[i] = l + amount * (c[i] - l);
		}
	}

	// 9. Curves: master, then each channel's own.
	const Vector<float> &master = p.curves[CG::CURVE_MASTER];
	for (int i = 0; i < 3; i++) {
		if (!master.is_empty()) {
			c[i] = _sample_curve(master, c[i]);
		}
		const Vector<float> &channel = p.curves[CG::CURVE_RED + i];
		if (!channel.is_empty()) {
			c[i] = _sample_curve(channel, c[i]);
		}
	}

	return Color(CLAMP(c[0], 0.0f, 1.0f), CLAMP(c[1], 0.0f, 1.0f), CLAMP(c[2], 0.0f, 1.0f), 1.0f);
}

Color RendererColorGrading::grade(const Params &p_params, const Color &p_srgb) {
	return _grade(p_params, ColorGradingLinear(p_params), p_srgb);
}

Vector<uint8_t> RendererColorGrading::bake(const Params &p_params) {
	const int n = LUT_SIZE;
	Vector<uint8_t> data;
	data.resize(n * n * n * 4 * sizeof(uint16_t));
	uint16_t *w = reinterpret_cast<uint16_t *>(data.ptrw());
	const float step = 1.0f / float(n - 1);
	const ColorGradingLinear linear(p_params);
	for (int k = 0; k < n; k++) {
		for (int j = 0; j < n; j++) {
			for (int i = 0; i < n; i++) {
				const Color graded = _grade(p_params, linear, Color(i * step, j * step, k * step));
				*w++ = Math::make_half_float(graded.r);
				*w++ = Math::make_half_float(graded.g);
				*w++ = Math::make_half_float(graded.b);
				*w++ = Math::make_half_float(1.0f);
			}
		}
	}
	return data;
}
