/**************************************************************************/
/*  metahuman_groom.h                                                     */
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

#include "core/templates/local_vector.h"
#include "core/variant/variant.h"

// A groom read from an Unreal groom Alembic file (Ogawa container, one AbcGeom curves object),
// in Godot space: meters, Y up, +Z front.
struct MetaHumanGroom {
	LocalVector<Vector3> points;
	// Curve c spans points [curve_offsets[c], curve_offsets[c + 1]). The first point is the root.
	LocalVector<uint32_t> curve_offsets;
	// Per point, in meters. Empty when the file has none.
	LocalVector<float> widths;
	// Per point, linear RGB (`groom_color`). Empty when the file has none.
	LocalVector<Color> colors;
	// Per curve: guides drive the simulation and are not rendered.
	LocalVector<uint8_t> is_guide;
	// Per curve: the root's UV on the scalp (`uv`). Empty when the file has none.
	LocalVector<Vector2> root_uvs;

	// Guide interpolation for rendered curves (`groom_sim_*`), kept for simulation: per curve, the two
	// guides and their weights; per point, the guide point each one follows and the lerp between
	// that point and the next.
	LocalVector<int32_t> sim_curve_a, sim_curve_b;
	LocalVector<float> sim_weight_a, sim_weight_b;
	LocalVector<int32_t> sim_vertex_a, sim_vertex_b;
	LocalVector<float> sim_lerp_a, sim_lerp_b;

	uint32_t get_curve_count() const { return curve_offsets.is_empty() ? 0 : curve_offsets.size() - 1; }
	uint32_t get_curve_size(uint32_t p_curve) const { return curve_offsets[p_curve + 1] - curve_offsets[p_curve]; }

	// Unreal grooms are centimeters, Z up, +Y front, in its left-handed frame: Godot's (x, y, z) is the
	// file's (x, z, y) / 100. The fit decided it: Ava's groom lash roots sit 0.46 mm from the DNA's eyelash
	// mesh this way, 0.59 mm with x mirrored (webgpu_notes/TASKS.md §2.33).
	static Vector3 to_godot(const float *p_file_xyz) { return Vector3(p_file_xyz[0], p_file_xyz[2], p_file_xyz[1]) * 0.01; }

	static Error load(const String &p_path, MetaHumanGroom &r_groom);
};
