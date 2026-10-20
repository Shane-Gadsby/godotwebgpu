/**************************************************************************/
/*  metahuman_hair.h                                                      */
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

#include "metahuman_groom.h"

#include "core/templates/hash_map.h"

// The skinned surface hair roots bind to (the head's LOD 0), with a spatial hash for nearest-vertex
// lookups. Bones are skeleton bone indices, 8 per vertex.
class MetaHumanSkinSurface {
	LocalVector<Vector3> positions;
	LocalVector<int> bones;
	LocalVector<float> weights;
	HashMap<Vector3i, LocalVector<uint32_t>> cells;
	float cell_size = 0.01;

	Vector3i _cell(const Vector3 &p_point) const;

public:
	void set_from_arrays(const Array &p_arrays);
	bool is_empty() const { return positions.is_empty(); }
	// Index of the vertex nearest p_point, or -1.
	int nearest(const Vector3 &p_point) const;
	void get_weights(int p_vertex, int *r_bones, float *r_weights) const;
};

namespace MetaHumanHair {

struct StrandSettings {
	// Fraction of the rendered (non-guide) curves to keep, picked evenly.
	float strand_fraction = 1.0;
	// Curves are resampled to at most this many points (0 keeps them as they are).
	int max_points = 0;
	// Writes CUSTOM0 = (guide a, guide b, weight of a, position along the curve) from the groom's guide
	// interpolation, for MetaHumanHairSimulator's offsets.
	bool with_guides = false;
};

// Builds camera-facing ribbons, two vertices per curve point: VERTEX the curve point, TANGENT its
// direction, TEX_UV (side, position along the curve), TEX_UV2 (width / r_max_width, a per-curve
// random), COLOR the groom color. Every vertex takes the skin weights of the scalp or skin vertex
// nearest its curve's root, so the strands follow the face joints with ordinary skinning.
bool build_strands(const MetaHumanGroom &p_groom, const MetaHumanSkinSurface &p_surface, const StrandSettings &p_settings, Array &r_arrays, float &r_max_width);

struct ShellSettings {
	float voxel_size = 0.003;
	int target_triangles = 8000;
	float strand_fraction = 0.35;
	// Voxels of closing (dilate then erode) that seal the gaps between strands.
	int closing = 3;
	// Blur passes over the closed volume: more makes a smoother, less clumpy surface.
	int smoothing = 4;
	// Connected pieces smaller than this fraction of the largest are dropped (stray strands).
	float min_piece_fraction = 0.02;
	// Passes of smoothing over the flow tangents.
	int flow_smoothing = 8;
	// Isosurface level in the smoothed 0..1 occupancy.
	float iso_level = 0.22;
	// When set (guide_count * guide_point_count points), each vertex follows its nearest guide point:
	// CUSTOM0 = (guide, guide, 1, position along it).
	const LocalVector<Vector3> *guide_points = nullptr;
	int guide_point_count = 0;
};

// Bakes the groom to one opaque mesh: the strands are voxelized, closed, smoothed, meshed with surface
// nets and simplified. TANGENT is the local strand direction (for an anisotropic highlight), COLOR the
// local groom color; skin weights come from the root of the curve that filled each voxel.
bool build_shell(const MetaHumanGroom &p_groom, const MetaHumanSkinSurface &p_surface, const ShellSettings &p_settings, Array &r_arrays);

// Curve p_curve resampled to exactly p_points points by arc length.
void resample_curve(const MetaHumanGroom &p_groom, uint32_t p_curve, int p_points, LocalVector<Vector3> &r_points);

} // namespace MetaHumanHair
