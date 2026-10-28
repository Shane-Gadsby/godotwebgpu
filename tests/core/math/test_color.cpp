/**************************************************************************/
/*  test_color.cpp                                                        */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_color)

#include "core/math/color.h"

namespace TestColor {

TEST_CASE("[Color] Constructor methods") {
	constexpr Color blue_rgba = Color(0.25098, 0.376471, 1, 0.501961);
	const Color blue_html = Color::html("#4060ff80");
	const Color blue_hex = Color::hex(0x4060ff80);
	const Color blue_hex64 = Color::hex64(0x4040'6060'ffff'8080);

	CHECK_MESSAGE(
			blue_rgba.is_equal_approx(blue_html),
			"Creation with HTML notation should result in components approximately equal to the default constructor.");
	CHECK_MESSAGE(
			blue_rgba.is_equal_approx(blue_hex),
			"Creation with a 32-bit hexadecimal number should result in components approximately equal to the default constructor.");
	CHECK_MESSAGE(
			blue_rgba.is_equal_approx(blue_hex64),
			"Creation with a 64-bit hexadecimal number should result in components approximately equal to the default constructor.");

	ERR_PRINT_OFF;
	const Color html_invalid = Color::html("invalid");
	ERR_PRINT_ON;

	CHECK_MESSAGE(
			html_invalid.is_equal_approx(Color()),
			"Creation with invalid HTML notation should result in a Color with the default values.");

	constexpr Color green_rgba = Color(0, 1, 0, 0.25);
	const Color green_hsva = Color::from_hsv(120 / 360.0, 1, 1, 0.25);

	CHECK_MESSAGE(
			green_rgba.is_equal_approx(green_hsva),
			"Creation with HSV notation should result in components approximately equal to the default constructor.");
}

TEST_CASE("[Color] Operators") {
	constexpr Color blue = Color(0.2, 0.2, 1);
	constexpr Color dark_red = Color(0.3, 0.1, 0.1);

	// Color components may be negative. Also, the alpha component may be greater than 1.0.
	CHECK_MESSAGE(
			(blue + dark_red).is_equal_approx(Color(0.5, 0.3, 1.1, 2)),
			"Color addition should behave as expected.");
	CHECK_MESSAGE(
			(blue - dark_red).is_equal_approx(Color(-0.1, 0.1, 0.9, 0)),
			"Color subtraction should behave as expected.");
	CHECK_MESSAGE(
			(blue * 2).is_equal_approx(Color(0.4, 0.4, 2, 2)),
			"Color multiplication with a scalar should behave as expected.");
	CHECK_MESSAGE(
			(blue / 2).is_equal_approx(Color(0.1, 0.1, 0.5, 0.5)),
			"Color division with a scalar should behave as expected.");
	CHECK_MESSAGE(
			(blue * dark_red).is_equal_approx(Color(0.06, 0.02, 0.1)),
			"Color multiplication with another Color should behave as expected.");
	CHECK_MESSAGE(
			(blue / dark_red).is_equal_approx(Color(0.666667, 2, 10)),
			"Color division with another Color should behave as expected.");
	CHECK_MESSAGE(
			(-blue).is_equal_approx(Color(0.8, 0.8, 0, 0)),
			"Color negation should behave as expected (affecting the alpha channel, unlike `invert()`).");
}

TEST_CASE("[Color] Hash") {
	const Color red = Color(1, 0, 0, 1);
	const Color yellow = Color(1, 1, 0, 1);

	CHECK(red.hash() != yellow.hash());
}

TEST_CASE("[Color] Reading methods") {
	constexpr Color dark_blue = Color(0, 0, 0.5, 0.4);

	CHECK_MESSAGE(
			dark_blue.get_h() == doctest::Approx(240.0f / 360.0f),
			"The returned HSV hue should match the expected value.");
	CHECK_MESSAGE(
			dark_blue.get_s() == doctest::Approx(1.0f),
			"The returned HSV saturation should match the expected value.");
	CHECK_MESSAGE(
			dark_blue.get_v() == doctest::Approx(0.5f),
			"The returned HSV value should match the expected value.");
}

TEST_CASE("[Color] Conversion methods") {
	constexpr Color cyan = Color(0, 1, 1);
	constexpr Color cyan_transparent = Color(0, 1, 1, 0);

	CHECK_MESSAGE(
			cyan.to_html() == "00ffffff",
			"The returned RGB HTML color code should match the expected value.");
	CHECK_MESSAGE(
			cyan_transparent.to_html() == "00ffff00",
			"The returned RGBA HTML color code should match the expected value.");
	CHECK_MESSAGE(
			cyan.to_argb32() == 0xff00ffff,
			"The returned 32-bit RGB number should match the expected value.");
	CHECK_MESSAGE(
			cyan.to_abgr32() == 0xffffff00,
			"The returned 32-bit BGR number should match the expected value.");
	CHECK_MESSAGE(
			cyan.to_rgba32() == 0x00ffffff,
			"The returned 32-bit BGR number should match the expected value.");
	CHECK_MESSAGE(
			cyan.to_argb64() == 0xffff'0000'ffff'ffff,
			"The returned 64-bit RGB number should match the expected value.");
	CHECK_MESSAGE(
			cyan.to_abgr64() == 0xffff'ffff'ffff'0000,
			"The returned 64-bit BGR number should match the expected value.");
	CHECK_MESSAGE(
			cyan.to_rgba64() == 0x0000'ffff'ffff'ffff,
			"The returned 64-bit BGR number should match the expected value.");
	CHECK_MESSAGE(
			String(cyan) == "(0.0, 1.0, 1.0, 1.0)",
			"The string representation should match the expected value.");
}

TEST_CASE("[Color] Linear <-> sRGB conversion") {
	constexpr Color color = Color(0.35, 0.5, 0.6, 0.7);
	const Color color_linear = color.srgb_to_linear();
	const Color color_srgb = color.linear_to_srgb();
	CHECK_MESSAGE(
			color_linear.is_equal_approx(Color(0.100481, 0.214041, 0.318547, 0.7)),
			"The color converted to linear color space should match the expected value.");
	CHECK_MESSAGE(
			color_srgb.is_equal_approx(Color(0.62621, 0.735357, 0.797738, 0.7)),
			"The color converted to sRGB color space should match the expected value.");
	CHECK_MESSAGE(
			color_linear.linear_to_srgb().is_equal_approx(Color(0.35, 0.5, 0.6, 0.7)),
			"The linear color converted back to sRGB color space should match the expected value.");
	CHECK_MESSAGE(
			color_srgb.srgb_to_linear().is_equal_approx(Color(0.35, 0.5, 0.6, 0.7)),
			"The sRGB color converted back to linear color space should match the expected value.");
	CHECK_MESSAGE(
			Color(1.0, 1.0, 1.0, 1.0).srgb_to_linear() == (Color(1.0, 1.0, 1.0, 1.0)),
			"White converted from sRGB to linear should remain white.");
	CHECK_MESSAGE(
			Color(1.0, 1.0, 1.0, 1.0).linear_to_srgb() == (Color(1.0, 1.0, 1.0, 1.0)),
			"White converted from linear to sRGB should remain white.");
}

TEST_CASE("[Color] Named colors") {
	CHECK_MESSAGE(
			Color::named("red").is_equal_approx(Color::hex(0xFF0000FF)),
			"The named color \"red\" should match the expected value.");

	// Named colors have their names automatically normalized.
	CHECK_MESSAGE(
			Color::named("white_smoke").is_equal_approx(Color::hex(0xF5F5F5FF)),
			"The named color \"white_smoke\" should match the expected value.");
	CHECK_MESSAGE(
			Color::named("Slate Blue").is_equal_approx(Color::hex(0x6A5ACDFF)),
			"The named color \"Slate Blue\" should match the expected value.");

	ERR_PRINT_OFF;
	CHECK_MESSAGE(
			Color::named("doesn't exist").is_equal_approx(Color()),
			"The invalid named color \"doesn't exist\" should result in a Color with the default values.");
	ERR_PRINT_ON;
}

TEST_CASE("[Color] Validation methods") {
	CHECK_MESSAGE(
			Color::html_is_valid("#4080ff"),
			"Valid HTML color (with leading #) should be considered valid.");
	CHECK_MESSAGE(
			Color::html_is_valid("4080ff"),
			"Valid HTML color (without leading #) should be considered valid.");
	CHECK_MESSAGE(
			!Color::html_is_valid("12345"),
			"Invalid HTML color should be considered invalid.");
	CHECK_MESSAGE(
			!Color::html_is_valid("#fuf"),
			"Invalid HTML color should be considered invalid.");
}

TEST_CASE("[Color] Manipulation methods") {
	constexpr Color blue = Color(0, 0, 1, 0.4);

	CHECK_MESSAGE(
			blue.inverted().is_equal_approx(Color(1, 1, 0, 0.4)),
			"Inverted color should have its red, green and blue components inverted.");

	constexpr Color purple = Color(0.5, 0.2, 0.5, 0.25);

	CHECK_MESSAGE(
			purple.lightened(0.2).is_equal_approx(Color(0.6, 0.36, 0.6, 0.25)),
			"Color should be lightened by the expected amount.");
	CHECK_MESSAGE(
			purple.darkened(0.2).is_equal_approx(Color(0.4, 0.16, 0.4, 0.25)),
			"Color should be darkened by the expected amount.");

	constexpr Color red = Color(1, 0, 0, 0.2);
	constexpr Color yellow = Color(1, 1, 0, 0.8);

	CHECK_MESSAGE(
			red.lerp(yellow, 0.5).is_equal_approx(Color(1, 0.5, 0, 0.5)),
			"Red interpolated with yellow should be orange (with interpolated alpha).");
}

TEST_CASE("[Color] from_temperature") {
	// Reference: camera-sim's CinemaSettings.light_color_for_temperature() (the Krystek 1985 fit, negative channels
	// clamped, Rec. 709 luminance 1) evaluated in double precision, before its linear_to_srgb().
	struct Reference {
		float kelvin;
		Color luminance;
	};
	const Reference references[] = {
		{ 1000.0f, Color(4.5707696, 0.0395056, 0.0) },
		{ 2000.0f, Color(2.5198718, 0.6470895, 0.0204549) },
		{ 3200.0f, Color(1.6789979, 0.8663202, 0.3248313) },
		{ 5600.0f, Color(1.1334610, 0.9721286, 0.8830995) },
		{ 6500.0f, Color(1.0439923, 0.9832943, 1.0359439) },
		{ 10000.0f, Color(0.8706426, 0.9951882, 1.4285707) },
		{ 15000.0f, Color(0.7844679, 0.9925169, 1.7087815) },
	};
	for (const Reference &reference : references) {
		const Color c = Color::from_temperature(reference.kelvin);
		INFO("kelvin: ", reference.kelvin, " got: ", String(c));
		CHECK(Math::abs(c.r - reference.luminance.r) < 1e-5f);
		CHECK(Math::abs(c.g - reference.luminance.g) < 1e-5f);
		CHECK(Math::abs(c.b - reference.luminance.b) < 1e-5f);
		CHECK(c.a == 1.0f);
	}

	for (float kelvin = 1000.0f; kelvin <= 15000.0f; kelvin += 250.0f) {
		INFO("kelvin: ", kelvin);
		const Color lum = Color::from_temperature(kelvin, Color::TEMPERATURE_NORMALIZATION_LUMINANCE);
		CHECK(Math::abs(0.2126f * lum.r + 0.7152f * lum.g + 0.0722f * lum.b - 1.0f) < 1e-5f);
		const Color max = Color::from_temperature(kelvin, Color::TEMPERATURE_NORMALIZATION_MAX_CHANNEL);
		CHECK(MAX(max.r, MAX(max.g, max.b)) == 1.0f);
		CHECK(MIN(max.r, MIN(max.g, max.b)) >= 0.0f);
		// Both normalizations are the same chromaticity.
		const float scale = lum.r / max.r;
		CHECK(Math::abs(max.g * scale - lum.g) < 1e-5f);
		CHECK(Math::abs(max.b * scale - lum.b) < 1e-5f);
	}

	// 6500 K is close to neutral (D65 sits slightly above the Planckian locus, so not exactly).
	const Color daylight = Color::from_temperature(6500.0f);
	CHECK(Math::abs(daylight.r - 1.0f) < 0.07f);
	CHECK(Math::abs(daylight.g - 1.0f) < 0.07f);
	CHECK(Math::abs(daylight.b - 1.0f) < 0.07f);

	// Lower temperatures are redder, higher ones bluer.
	CHECK(Color::from_temperature(3200.0f).r > Color::from_temperature(5600.0f).r);
	CHECK(Color::from_temperature(3200.0f).b < Color::from_temperature(5600.0f).b);

	// Clamped to the fit's range.
	CHECK(Color::from_temperature(500.0f) == Color::from_temperature(1000.0f));
	CHECK(Color::from_temperature(40000.0f) == Color::from_temperature(15000.0f));
}

} // namespace TestColor
