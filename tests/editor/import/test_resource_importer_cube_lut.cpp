/**************************************************************************/
/*  test_resource_importer_cube_lut.cpp                                   */
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

TEST_FORCE_LINK(test_resource_importer_cube_lut)

#ifdef TOOLS_ENABLED

#include "editor/import/resource_importer_cube_lut.h"

// WebGPU fork: the .cube LUT parser behind CameraAttributes.lut_texture.
namespace TestResourceImporterCubeLUT {

static Color entry(const Vector<Color> &p_table, int p_size, int p_r, int p_g, int p_b) {
	return p_table[(p_b * p_size + p_g) * p_size + p_r];
}

TEST_CASE("[Editor][ResourceImporterCubeLUT] 3D table is kept as is, red fastest") {
	const String text = "# comment\nTITLE \"test\"\nLUT_3D_SIZE 2\n"
						"0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
						"0 0 1\n1 0 1\n0 1 1\n0.25 0.5 0.75\n";
	int size = 0;
	Vector<Color> table;
	String error;
	REQUIRE(ResourceImporterCubeLUT::parse_cube(text, size, table, error) == OK);
	CHECK(size == 2);
	CHECK(entry(table, size, 1, 0, 0).is_equal_approx(Color(1, 0, 0)));
	CHECK(entry(table, size, 0, 1, 0).is_equal_approx(Color(0, 1, 0)));
	CHECK(entry(table, size, 0, 0, 1).is_equal_approx(Color(0, 0, 1)));
	CHECK(entry(table, size, 1, 1, 1).is_equal_approx(Color(0.25, 0.5, 0.75)));
}

TEST_CASE("[Editor][ResourceImporterCubeLUT] 1D table becomes a 3D grid applying each channel's curve") {
	// Squares each channel: 0, 0.25, 1 at 0, 0.5, 1.
	const String text = "LUT_1D_SIZE 3\n0 0 0\n0.25 0.25 0.25\n1 1 1\n";
	int size = 0;
	Vector<Color> table;
	String error;
	REQUIRE(ResourceImporterCubeLUT::parse_cube(text, size, table, error) == OK);
	CHECK(size == 65);
	CHECK(table.size() == 65 * 65 * 65);
	const Color mid = entry(table, size, 32, 16, 64); // Inputs 0.5, 0.25, 1.
	CHECK(mid.r == doctest::Approx(0.25));
	CHECK(mid.g == doctest::Approx(0.125)); // Linear between the 1D entries 0 and 0.25.
	CHECK(mid.b == doctest::Approx(1.0));
}

TEST_CASE("[Editor][ResourceImporterCubeLUT] DOMAIN_MIN/MAX remaps the input, clamping outside it") {
	// Identity over 0-2: input 1 lands mid-table and returns 1; input 0.5 returns 0.5.
	const String text = "LUT_1D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\n0 0 0\n2 2 2\n";
	int size = 0;
	Vector<Color> table;
	String error;
	REQUIRE(ResourceImporterCubeLUT::parse_cube(text, size, table, error) == OK);
	CHECK(entry(table, size, 64, 32, 0).r == doctest::Approx(1.0));
	CHECK(entry(table, size, 64, 32, 0).g == doctest::Approx(0.5));
}

TEST_CASE("[Editor][ResourceImporterCubeLUT] Shaper then 3D table") {
	// The shaper inverts each channel; the 3D table is the identity, so the result is 1 - input.
	const String text = "LUT_1D_SIZE 2\nLUT_3D_SIZE 2\n1 1 1\n0 0 0\n"
						"0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
	int size = 0;
	Vector<Color> table;
	String error;
	REQUIRE(ResourceImporterCubeLUT::parse_cube(text, size, table, error) == OK);
	CHECK(size == 33);
	const Color c = entry(table, size, 8, 16, 32); // Inputs 0.25, 0.5, 1.
	CHECK(c.r == doctest::Approx(0.75));
	CHECK(c.g == doctest::Approx(0.5));
	CHECK(c.b == doctest::Approx(0.0));
}

TEST_CASE("[Editor][ResourceImporterCubeLUT] Malformed files are rejected with the line") {
	int size = 0;
	Vector<Color> table;
	String error;
	ERR_PRINT_OFF;
	CHECK(ResourceImporterCubeLUT::parse_cube("LUT_3D_SIZE 2\n0 0 0\n", size, table, error) == ERR_PARSE_ERROR);
	CHECK(error.contains("Expected 8"));
	CHECK(ResourceImporterCubeLUT::parse_cube("LUT_3D_SIZE 2\nBOGUS 1\n", size, table, error) == ERR_PARSE_ERROR);
	CHECK(error.contains("Line 2"));
	CHECK(ResourceImporterCubeLUT::parse_cube("0 0 0\n", size, table, error) == ERR_PARSE_ERROR);
	CHECK(ResourceImporterCubeLUT::parse_cube("LUT_3D_SIZE 1\n0 0 0\n", size, table, error) == ERR_PARSE_ERROR);
	ERR_PRINT_ON;
}

} // namespace TestResourceImporterCubeLUT

#endif // TOOLS_ENABLED
