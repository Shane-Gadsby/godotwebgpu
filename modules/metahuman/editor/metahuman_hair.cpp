/**************************************************************************/
/*  metahuman_hair.cpp                                                    */
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

#include "metahuman_hair.h"

#include "scene/resources/mesh.h"
#include "scene/resources/surface_tool.h"

// Skin surface ------------------------------------------------------------------------------------

Vector3i MetaHumanSkinSurface::_cell(const Vector3 &p_point) const {
	return Vector3i(Math::floor(p_point.x / cell_size), Math::floor(p_point.y / cell_size), Math::floor(p_point.z / cell_size));
}

void MetaHumanSkinSurface::set_from_arrays(const Array &p_arrays) {
	const PackedVector3Array vertices = p_arrays[Mesh::ARRAY_VERTEX];
	const PackedInt32Array arr_bones = p_arrays[Mesh::ARRAY_BONES];
	const PackedFloat32Array arr_weights = p_arrays[Mesh::ARRAY_WEIGHTS];
	ERR_FAIL_COND(arr_bones.size() != vertices.size() * 8 || arr_weights.size() != vertices.size() * 8);
	positions.resize(vertices.size());
	bones.resize(arr_bones.size());
	weights.resize(arr_weights.size());
	cells.clear();
	for (int i = 0; i < vertices.size(); i++) {
		positions[i] = vertices[i];
		cells[_cell(vertices[i])].push_back(i);
	}
	memcpy(bones.ptr(), arr_bones.ptr(), bones.size() * sizeof(int));
	memcpy(weights.ptr(), arr_weights.ptr(), weights.size() * sizeof(float));
}

int MetaHumanSkinSurface::nearest(const Vector3 &p_point) const {
	const Vector3i center = _cell(p_point);
	int best = -1;
	real_t best_distance = 1e20;
	// Grow the search until a hit, then one ring further: the nearest vertex can be in the next ring.
	for (int radius = 0; radius < 64; radius++) {
		for (int x = -radius; x <= radius; x++) {
			for (int y = -radius; y <= radius; y++) {
				for (int z = -radius; z <= radius; z++) {
					if (MAX(Math::abs(x), MAX(Math::abs(y), Math::abs(z))) != radius) {
						continue;
					}
					const LocalVector<uint32_t> *list = cells.getptr(center + Vector3i(x, y, z));
					if (!list) {
						continue;
					}
					for (uint32_t v : *list) {
						const real_t d = positions[v].distance_squared_to(p_point);
						if (d < best_distance) {
							best_distance = d;
							best = v;
						}
					}
				}
			}
		}
		if (best >= 0 && Math::sqrt(best_distance) < radius * cell_size) {
			break;
		}
	}
	return best;
}

void MetaHumanSkinSurface::get_weights(int p_vertex, int *r_bones, float *r_weights) const {
	for (int k = 0; k < 8; k++) {
		r_bones[k] = p_vertex >= 0 ? bones[p_vertex * 8 + k] : 0;
		r_weights[k] = p_vertex >= 0 ? weights[p_vertex * 8 + k] : (k == 0 ? 1.0f : 0.0f);
	}
}

namespace {

bool _keep_curve(uint32_t p_curve, float p_fraction) {
	if (p_fraction >= 1.0) {
		return true;
	}
	// Knuth's multiplicative hash spreads the kept curves evenly over the scalp.
	return float((p_curve * 2654435761u) % 10000u) < p_fraction * 10000.0f;
}

// Points of one curve, resampled to at most p_max_points by arc length.
void _curve_points(const MetaHumanGroom &p_groom, uint32_t p_curve, int p_max_points, LocalVector<Vector3> &r_points, LocalVector<float> &r_widths, LocalVector<Color> &r_colors) {
	const uint32_t begin = p_groom.curve_offsets[p_curve];
	const uint32_t count = p_groom.get_curve_size(p_curve);
	r_points.clear();
	r_widths.clear();
	r_colors.clear();
	const bool has_widths = !p_groom.widths.is_empty();
	const bool has_colors = !p_groom.colors.is_empty();
	if (p_max_points <= 1 || count <= (uint32_t)p_max_points) {
		for (uint32_t i = 0; i < count; i++) {
			r_points.push_back(p_groom.points[begin + i]);
			r_widths.push_back(has_widths ? p_groom.widths[begin + i] : 0.0001f);
			r_colors.push_back(has_colors ? p_groom.colors[begin + i] : Color(1, 1, 1));
		}
		return;
	}
	LocalVector<float> lengths;
	lengths.resize(count);
	lengths[0] = 0.0;
	for (uint32_t i = 1; i < count; i++) {
		lengths[i] = lengths[i - 1] + p_groom.points[begin + i].distance_to(p_groom.points[begin + i - 1]);
	}
	const float total = lengths[count - 1];
	uint32_t segment = 0;
	for (int s = 0; s < p_max_points; s++) {
		const float target = total * s / (p_max_points - 1);
		while (segment + 2 < count && lengths[segment + 1] < target) {
			segment++;
		}
		const float span = MAX(lengths[segment + 1] - lengths[segment], 1e-9f);
		const float t = CLAMP((target - lengths[segment]) / span, 0.0f, 1.0f);
		r_points.push_back(p_groom.points[begin + segment].lerp(p_groom.points[begin + segment + 1], t));
		r_widths.push_back(has_widths ? Math::lerp(p_groom.widths[begin + segment], p_groom.widths[begin + segment + 1], t) : 0.0001f);
		r_colors.push_back(has_colors ? p_groom.colors[begin + segment].lerp(p_groom.colors[begin + segment + 1], t) : Color(1, 1, 1));
	}
}

Vector3 _perpendicular(const Vector3 &p_dir) {
	const Vector3 axis = Math::abs(p_dir.y) < 0.9 ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
	return p_dir.cross(axis).normalized();
}

} // namespace

bool MetaHumanHair::build_strands(const MetaHumanGroom &p_groom, const MetaHumanSkinSurface &p_surface, const StrandSettings &p_settings, Array &r_arrays, float &r_max_width) {
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedFloat32Array tangents;
	PackedVector2Array uvs;
	PackedVector2Array uv2s;
	PackedColorArray colors;
	PackedInt32Array bones;
	PackedFloat32Array weights;
	PackedInt32Array indices;
	PackedFloat32Array custom0;
	const bool guided = p_settings.with_guides && p_groom.sim_curve_a.size() == p_groom.get_curve_count() && p_groom.sim_weight_a.size() == p_groom.get_curve_count();

	r_max_width = 0.0;
	for (float w : p_groom.widths) {
		r_max_width = MAX(r_max_width, w);
	}
	if (r_max_width <= 0.0) {
		r_max_width = 0.0001;
	}

	LocalVector<Vector3> points;
	LocalVector<float> widths;
	LocalVector<Color> point_colors;
	int root_bones[8];
	float root_weights[8];
	for (uint32_t c = 0; c < p_groom.get_curve_count(); c++) {
		if (p_groom.is_guide[c] || !_keep_curve(c, p_settings.strand_fraction) || p_groom.get_curve_size(c) < 2) {
			continue;
		}
		_curve_points(p_groom, c, p_settings.max_points, points, widths, point_colors);
		p_surface.get_weights(p_surface.nearest(points[0]), root_bones, root_weights);
		const float seed = float((c * 2246822519u) % 1000u) / 999.0f;

		float total = 0.0;
		for (uint32_t i = 1; i < points.size(); i++) {
			total += points[i].distance_to(points[i - 1]);
		}
		float along = 0.0;
		const int first = vertices.size();
		for (uint32_t i = 0; i < points.size(); i++) {
			if (i > 0) {
				along += points[i].distance_to(points[i - 1]);
			}
			const Vector3 prev = points[i > 0 ? i - 1 : i];
			const Vector3 next = points[i + 1 < points.size() ? i + 1 : i];
			Vector3 dir = (next - prev).normalized();
			if (dir.is_zero_approx()) {
				dir = Vector3(0, -1, 0);
			}
			const Vector3 normal = _perpendicular(dir);
			const float t = total > 0.0 ? along / total : 0.0;
			for (int side = 0; side < 2; side++) {
				vertices.push_back(points[i]);
				normals.push_back(normal);
				tangents.push_back(dir.x);
				tangents.push_back(dir.y);
				tangents.push_back(dir.z);
				tangents.push_back(1.0);
				uvs.push_back(Vector2(side, t));
				uv2s.push_back(Vector2(widths[i] / r_max_width, seed));
				colors.push_back(point_colors[i]);
				for (int k = 0; k < 8; k++) {
					bones.push_back(root_bones[k]);
					weights.push_back(root_weights[k]);
				}
				if (guided) {
					const int a = MAX(p_groom.sim_curve_a[c], 0);
					const int b = c < p_groom.sim_curve_b.size() && p_groom.sim_curve_b[c] >= 0 ? p_groom.sim_curve_b[c] : a;
					custom0.push_back(a);
					custom0.push_back(b);
					custom0.push_back(p_groom.sim_curve_b.size() ? p_groom.sim_weight_a[c] : 1.0f);
					custom0.push_back(t);
				}
			}
		}
		for (uint32_t i = 0; i + 1 < points.size(); i++) {
			const int a = first + i * 2;
			const int b = a + 2;
			indices.push_back(a);
			indices.push_back(b);
			indices.push_back(a + 1);
			indices.push_back(a + 1);
			indices.push_back(b);
			indices.push_back(b + 1);
		}
	}
	if (indices.is_empty()) {
		return false;
	}
	r_arrays.resize(Mesh::ARRAY_MAX);
	r_arrays[Mesh::ARRAY_VERTEX] = vertices;
	r_arrays[Mesh::ARRAY_NORMAL] = normals;
	r_arrays[Mesh::ARRAY_TANGENT] = tangents;
	r_arrays[Mesh::ARRAY_TEX_UV] = uvs;
	r_arrays[Mesh::ARRAY_TEX_UV2] = uv2s;
	r_arrays[Mesh::ARRAY_COLOR] = colors;
	r_arrays[Mesh::ARRAY_BONES] = bones;
	r_arrays[Mesh::ARRAY_WEIGHTS] = weights;
	r_arrays[Mesh::ARRAY_INDEX] = indices;
	if (guided) {
		r_arrays[Mesh::ARRAY_CUSTOM0] = custom0;
	}
	return true;
}

void MetaHumanHair::resample_curve(const MetaHumanGroom &p_groom, uint32_t p_curve, int p_points, LocalVector<Vector3> &r_points) {
	LocalVector<float> widths;
	LocalVector<Color> colors;
	_curve_points(p_groom, p_curve, p_points, r_points, widths, colors);
	// _curve_points() keeps short curves as they are; pad them to exactly p_points.
	while ((int)r_points.size() < p_points) {
		r_points.push_back(r_points[r_points.size() - 1]);
	}
}

// Shell --------------------------------------------------------------------------------------------

namespace {

struct Grid {
	Vector3i size;
	Vector3 origin;
	float voxel = 0.003;

	int index(int p_x, int p_y, int p_z) const { return (p_z * size.y + p_y) * size.x + p_x; }
	int count() const { return size.x * size.y * size.z; }
	bool inside(int p_x, int p_y, int p_z) const { return p_x >= 0 && p_y >= 0 && p_z >= 0 && p_x < size.x && p_y < size.y && p_z < size.z; }
	Vector3 point(float p_x, float p_y, float p_z) const { return origin + Vector3(p_x, p_y, p_z) * voxel; }
};

// One pass of a separable 3-wide max (dilate) or min (erode) along each axis.
void _morph(const Grid &p_grid, LocalVector<uint8_t> &r_cells, bool p_dilate) {
	LocalVector<uint8_t> tmp;
	for (int axis = 0; axis < 3; axis++) {
		tmp = r_cells;
		for (int z = 0; z < p_grid.size.z; z++) {
			for (int y = 0; y < p_grid.size.y; y++) {
				for (int x = 0; x < p_grid.size.x; x++) {
					uint8_t v = tmp[p_grid.index(x, y, z)];
					for (int d = -1; d <= 1; d += 2) {
						const int nx = x + (axis == 0 ? d : 0);
						const int ny = y + (axis == 1 ? d : 0);
						const int nz = z + (axis == 2 ? d : 0);
						const uint8_t n = p_grid.inside(nx, ny, nz) ? tmp[p_grid.index(nx, ny, nz)] : 0;
						v = p_dilate ? MAX(v, n) : MIN(v, n);
					}
					r_cells[p_grid.index(x, y, z)] = v;
				}
			}
		}
	}
}

void _blur(const Grid &p_grid, LocalVector<float> &r_field) {
	LocalVector<float> tmp;
	for (int axis = 0; axis < 3; axis++) {
		tmp = r_field;
		for (int z = 0; z < p_grid.size.z; z++) {
			for (int y = 0; y < p_grid.size.y; y++) {
				for (int x = 0; x < p_grid.size.x; x++) {
					float sum = 0.0;
					for (int d = -1; d <= 1; d++) {
						const int nx = x + (axis == 0 ? d : 0);
						const int ny = y + (axis == 1 ? d : 0);
						const int nz = z + (axis == 2 ? d : 0);
						sum += (d == 0 ? 2.0f : 1.0f) * (p_grid.inside(nx, ny, nz) ? tmp[p_grid.index(nx, ny, nz)] : 0.0f);
					}
					r_field[p_grid.index(x, y, z)] = sum * 0.25f;
				}
			}
		}
	}
}

// Clears every 6-connected piece of the volume smaller than p_fraction of the largest one.
void _drop_small_pieces(const Grid &p_grid, LocalVector<uint8_t> &r_cells, float p_fraction) {
	LocalVector<int32_t> piece;
	piece.resize(p_grid.count());
	for (int i = 0; i < p_grid.count(); i++) {
		piece[i] = -1;
	}
	LocalVector<int> sizes;
	LocalVector<int> stack;
	for (int start = 0; start < p_grid.count(); start++) {
		if (!r_cells[start] || piece[start] >= 0) {
			continue;
		}
		const int id = sizes.size();
		int size = 0;
		stack.push_back(start);
		piece[start] = id;
		while (!stack.is_empty()) {
			const int idx = stack[stack.size() - 1];
			stack.resize(stack.size() - 1);
			size++;
			const int x = idx % p_grid.size.x;
			const int y = (idx / p_grid.size.x) % p_grid.size.y;
			const int z = idx / (p_grid.size.x * p_grid.size.y);
			static const int offsets[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
			for (const int *o : offsets) {
				const int nx = x + o[0];
				const int ny = y + o[1];
				const int nz = z + o[2];
				if (!p_grid.inside(nx, ny, nz)) {
					continue;
				}
				const int n = p_grid.index(nx, ny, nz);
				if (r_cells[n] && piece[n] < 0) {
					piece[n] = id;
					stack.push_back(n);
				}
			}
		}
		sizes.push_back(size);
	}
	int largest = 0;
	for (int size : sizes) {
		largest = MAX(largest, size);
	}
	const int minimum = (int)(largest * p_fraction);
	for (int i = 0; i < p_grid.count(); i++) {
		if (r_cells[i] && sizes[piece[i]] < minimum) {
			r_cells[i] = 0;
		}
	}
}

float _sample(const Grid &p_grid, const LocalVector<float> &p_field, int p_x, int p_y, int p_z) {
	p_x = CLAMP(p_x, 0, p_grid.size.x - 1);
	p_y = CLAMP(p_y, 0, p_grid.size.y - 1);
	p_z = CLAMP(p_z, 0, p_grid.size.z - 1);
	return p_field[p_grid.index(p_x, p_y, p_z)];
}

} // namespace

bool MetaHumanHair::build_shell(const MetaHumanGroom &p_groom, const MetaHumanSkinSurface &p_surface, const ShellSettings &p_settings, Array &r_arrays) {
	// Bounds of the kept curves, padded for the closing and the blur.
	AABB bounds;
	bool first = true;
	for (uint32_t c = 0; c < p_groom.get_curve_count(); c++) {
		if (p_groom.is_guide[c] || !_keep_curve(c, p_settings.strand_fraction)) {
			continue;
		}
		for (uint32_t i = p_groom.curve_offsets[c]; i < p_groom.curve_offsets[c + 1]; i++) {
			if (first) {
				bounds = AABB(p_groom.points[i], Vector3());
				first = false;
			} else {
				bounds.expand_to(p_groom.points[i]);
			}
		}
	}
	ERR_FAIL_COND_V(first, false);

	Grid grid;
	grid.voxel = MAX(p_settings.voxel_size, 0.0005f);
	const int pad = p_settings.closing + 3;
	grid.origin = bounds.position - Vector3(pad, pad, pad) * grid.voxel;
	grid.size = Vector3i((bounds.size / grid.voxel).ceil()) + Vector3i(pad * 2 + 1, pad * 2 + 1, pad * 2 + 1);
	ERR_FAIL_COND_V_MSG(grid.count() > 64 * 1024 * 1024, false, "MetaHuman: the hair shell grid is too large; raise the shell voxel size.");

	// Voxelize the strands; keep per voxel the strand direction, color and a curve filling it.
	LocalVector<uint8_t> occupied;
	LocalVector<Vector3> direction;
	LocalVector<Color> color;
	LocalVector<uint16_t> hits;
	LocalVector<int32_t> owner;
	occupied.resize(grid.count());
	direction.resize(grid.count());
	color.resize(grid.count());
	hits.resize(grid.count());
	owner.resize(grid.count());
	for (int i = 0; i < grid.count(); i++) {
		occupied[i] = 0;
		direction[i] = Vector3();
		color[i] = Color(0, 0, 0, 0);
		hits[i] = 0;
		owner[i] = -1;
	}
	const bool has_colors = !p_groom.colors.is_empty();
	for (uint32_t c = 0; c < p_groom.get_curve_count(); c++) {
		if (p_groom.is_guide[c] || !_keep_curve(c, p_settings.strand_fraction)) {
			continue;
		}
		for (uint32_t i = p_groom.curve_offsets[c]; i + 1 < p_groom.curve_offsets[c + 1]; i++) {
			const Vector3 a = p_groom.points[i];
			const Vector3 b = p_groom.points[i + 1];
			const Vector3 dir = (b - a).normalized();
			const int steps = MAX(1, (int)Math::ceil(a.distance_to(b) / (grid.voxel * 0.5)));
			for (int s = 0; s <= steps; s++) {
				const Vector3 p = (a.lerp(b, float(s) / steps) - grid.origin) / grid.voxel;
				const int x = (int)p.x;
				const int y = (int)p.y;
				const int z = (int)p.z;
				if (!grid.inside(x, y, z)) {
					continue;
				}
				const int idx = grid.index(x, y, z);
				occupied[idx] = 1;
				direction[idx] += dir;
				if (has_colors) {
					color[idx] += p_groom.colors[i];
				}
				if (hits[idx] < UINT16_MAX) {
					hits[idx]++;
				}
				owner[idx] = c;
			}
		}
	}

	// Close the gaps between strands, then smooth to a field for the isosurface.
	for (int i = 0; i < p_settings.closing; i++) {
		_morph(grid, occupied, true);
	}
	for (int i = 0; i < p_settings.closing; i++) {
		_morph(grid, occupied, false);
	}
	_drop_small_pieces(grid, occupied, p_settings.min_piece_fraction);
	LocalVector<float> field;
	field.resize(grid.count());
	for (int i = 0; i < grid.count(); i++) {
		field[i] = occupied[i];
	}
	for (int i = 0; i < p_settings.smoothing; i++) {
		_blur(grid, field);
	}
	// Below 0.5 the surface sits a little outside the strands: where the hair layer is under a voxel
	// thick (the hairline) a 0.5 surface opens holes onto the scalp.
	const float iso = p_settings.iso_level;

	// Surface nets: one vertex per cell the surface crosses, at the mean of its edge crossings; one
	// quad per crossed grid edge, between the four cells around it.
	static const int corner[8][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 0, 0, 1 }, { 1, 0, 1 }, { 0, 1, 1 }, { 1, 1, 1 } };
	static const int edge[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
	HashMap<int, int> cell_vertex;
	LocalVector<Vector3> positions;
	LocalVector<Vector3i> position_cells;
	for (int z = 0; z + 1 < grid.size.z; z++) {
		for (int y = 0; y + 1 < grid.size.y; y++) {
			for (int x = 0; x + 1 < grid.size.x; x++) {
				float v[8];
				int inside_count = 0;
				for (int k = 0; k < 8; k++) {
					v[k] = field[grid.index(x + corner[k][0], y + corner[k][1], z + corner[k][2])];
					inside_count += v[k] > iso;
				}
				if (inside_count == 0 || inside_count == 8) {
					continue;
				}
				Vector3 sum;
				int crossings = 0;
				for (int e = 0; e < 12; e++) {
					const float va = v[edge[e][0]];
					const float vb = v[edge[e][1]];
					if ((va > iso) == (vb > iso)) {
						continue;
					}
					const float t = (iso - va) / (vb - va);
					const int *ca = corner[edge[e][0]];
					const int *cb = corner[edge[e][1]];
					sum += Vector3(Math::lerp((float)ca[0], (float)cb[0], t), Math::lerp((float)ca[1], (float)cb[1], t), Math::lerp((float)ca[2], (float)cb[2], t));
					crossings++;
				}
				cell_vertex.insert(grid.index(x, y, z), positions.size());
				const Vector3 local = sum / crossings;
				positions.push_back(grid.point(x + local.x, y + local.y, z + local.z));
				position_cells.push_back(Vector3i(x, y, z));
			}
		}
	}

	LocalVector<uint32_t> quads_indices;
	for (int z = 1; z + 1 < grid.size.z; z++) {
		for (int y = 1; y + 1 < grid.size.y; y++) {
			for (int x = 1; x + 1 < grid.size.x; x++) {
				const bool here = field[grid.index(x, y, z)] > iso;
				for (int axis = 0; axis < 3; axis++) {
					const int nx = x + (axis == 0);
					const int ny = y + (axis == 1);
					const int nz = z + (axis == 2);
					if (here == (field[grid.index(nx, ny, nz)] > iso)) {
						continue;
					}
					// The four cells sharing this edge.
					Vector3i c[4];
					if (axis == 0) {
						c[0] = Vector3i(x, y - 1, z - 1);
						c[1] = Vector3i(x, y, z - 1);
						c[2] = Vector3i(x, y, z);
						c[3] = Vector3i(x, y - 1, z);
					} else if (axis == 1) {
						c[0] = Vector3i(x - 1, y, z - 1);
						c[1] = Vector3i(x - 1, y, z);
						c[2] = Vector3i(x, y, z);
						c[3] = Vector3i(x, y, z - 1);
					} else {
						c[0] = Vector3i(x - 1, y - 1, z);
						c[1] = Vector3i(x, y - 1, z);
						c[2] = Vector3i(x, y, z);
						c[3] = Vector3i(x - 1, y, z);
					}
					int q[4];
					bool complete = true;
					for (int k = 0; k < 4 && complete; k++) {
						const int *vi = cell_vertex.getptr(grid.index(c[k].x, c[k].y, c[k].z));
						complete = vi != nullptr;
						q[k] = complete ? *vi : 0;
					}
					if (!complete) {
						continue;
					}
					// Wind every quad the same way relative to the outside (the field falls outward), with
					// Godot's clockwise front faces.
					const Vector3 outward = here ? Vector3(axis == 0, axis == 1, axis == 2) : -Vector3(axis == 0, axis == 1, axis == 2);
					const Vector3 n = (positions[q[1]] - positions[q[0]]).cross(positions[q[2]] - positions[q[0]]);
					if (n.dot(outward) > 0) {
						SWAP(q[1], q[3]);
					}
					quads_indices.push_back(q[0]);
					quads_indices.push_back(q[1]);
					quads_indices.push_back(q[2]);
					quads_indices.push_back(q[0]);
					quads_indices.push_back(q[2]);
					quads_indices.push_back(q[3]);
				}
			}
		}
	}
	ERR_FAIL_COND_V_MSG(quads_indices.is_empty(), false, "MetaHuman: the hair shell came out empty.");

	// Simplify to the triangle budget.
	LocalVector<uint32_t> simplified;
	simplified.resize(quads_indices.size());
	memcpy(simplified.ptr(), quads_indices.ptr(), quads_indices.size() * sizeof(uint32_t));
	if (SurfaceTool::simplify_func && p_settings.target_triangles > 0 && (int)quads_indices.size() > p_settings.target_triangles * 3) {
		LocalVector<float> flat;
		flat.resize(positions.size() * 3);
		for (uint32_t i = 0; i < positions.size(); i++) {
			flat[i * 3] = positions[i].x;
			flat[i * 3 + 1] = positions[i].y;
			flat[i * 3 + 2] = positions[i].z;
		}
		float error = 0.0;
		const size_t count = SurfaceTool::simplify_func(simplified.ptr(), quads_indices.ptr(), quads_indices.size(), flat.ptr(), positions.size(), sizeof(float) * 3,
				p_settings.target_triangles * 3, 1.0, 0, &error);
		simplified.resize(count);
	}

	// Compact, then per vertex: normal from the field's gradient, flow tangent, color and skin weights
	// from the voxels behind the surface.
	HashMap<uint32_t, int> remap;
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedFloat32Array tangents;
	PackedColorArray colors;
	PackedInt32Array bones;
	PackedFloat32Array weights;
	PackedInt32Array indices;
	HashMap<int32_t, int> curve_root_vertex;
	int root_bones[8];
	float root_weights[8];
	PackedFloat32Array custom0;
	const bool guided = p_settings.guide_points && p_settings.guide_point_count > 1 && !p_settings.guide_points->is_empty();
	// Guide points by grid cell, for the nearest-guide lookups.
	HashMap<Vector3i, LocalVector<uint32_t>> guide_cells;
	const float guide_cell = 0.02;
	if (guided) {
		for (uint32_t i = 0; i < p_settings.guide_points->size(); i++) {
			const Vector3 &q = (*p_settings.guide_points)[i];
			guide_cells[Vector3i(Math::floor(q.x / guide_cell), Math::floor(q.y / guide_cell), Math::floor(q.z / guide_cell))].push_back(i);
		}
	}
	for (uint32_t i = 0; i < simplified.size(); i++) {
		const uint32_t source = simplified[i];
		HashMap<uint32_t, int>::Iterator E = remap.find(source);
		if (!E) {
			E = remap.insert(source, vertices.size());
			const Vector3 p = positions[source];
			const Vector3i cell = position_cells[source];
			const Vector3 gradient = Vector3(
					_sample(grid, field, cell.x + 1, cell.y, cell.z) - _sample(grid, field, cell.x - 1, cell.y, cell.z),
					_sample(grid, field, cell.x, cell.y + 1, cell.z) - _sample(grid, field, cell.x, cell.y - 1, cell.z),
					_sample(grid, field, cell.x, cell.y, cell.z + 1) - _sample(grid, field, cell.x, cell.y, cell.z - 1));
			const Vector3 normal = gradient.is_zero_approx() ? Vector3(0, 1, 0) : -gradient.normalized();

			// The best-filled voxel near the surface point carries the strands under it.
			int best = -1;
			for (int dz = -2; dz <= 2; dz++) {
				for (int dy = -2; dy <= 2; dy++) {
					for (int dx = -2; dx <= 2; dx++) {
						const int x = cell.x + dx;
						const int y = cell.y + dy;
						const int z = cell.z + dz;
						if (!grid.inside(x, y, z)) {
							continue;
						}
						const int idx = grid.index(x, y, z);
						if (hits[idx] > 0 && (best < 0 || hits[idx] > hits[best])) {
							best = idx;
						}
					}
				}
			}
			Vector3 flow = best >= 0 ? direction[best] : Vector3();
			flow -= normal * flow.dot(normal);
			flow = flow.is_zero_approx() ? _perpendicular(normal) : flow.normalized();
			Color c = best >= 0 && has_colors ? color[best] / MAX((float)hits[best], 1.0f) : Color(1, 1, 1);
			c.a = 1.0;

			const int32_t curve = best >= 0 ? owner[best] : -1;
			int root_vertex = -1;
			if (curve >= 0) {
				if (const int *cached = curve_root_vertex.getptr(curve)) {
					root_vertex = *cached;
				} else {
					root_vertex = p_surface.nearest(p_groom.points[p_groom.curve_offsets[curve]]);
					curve_root_vertex.insert(curve, root_vertex);
				}
			} else {
				root_vertex = p_surface.nearest(p);
			}
			p_surface.get_weights(root_vertex, root_bones, root_weights);

			vertices.push_back(p);
			normals.push_back(normal);
			tangents.push_back(flow.x);
			tangents.push_back(flow.y);
			tangents.push_back(flow.z);
			tangents.push_back(1.0);
			colors.push_back(c);
			for (int k = 0; k < 8; k++) {
				bones.push_back(root_bones[k]);
				weights.push_back(root_weights[k]);
			}
			if (guided) {
				const Vector3i center(Math::floor(p.x / guide_cell), Math::floor(p.y / guide_cell), Math::floor(p.z / guide_cell));
				int best_point = -1;
				real_t best_distance = 1e20;
				for (int radius = 0; radius < 16 && best_point < 0; radius++) {
					for (int dz = -radius; dz <= radius; dz++) {
						for (int dy = -radius; dy <= radius; dy++) {
							for (int dx = -radius; dx <= radius; dx++) {
								if (MAX(Math::abs(dx), MAX(Math::abs(dy), Math::abs(dz))) != radius) {
									continue;
								}
								const LocalVector<uint32_t> *list = guide_cells.getptr(center + Vector3i(dx, dy, dz));
								if (!list) {
									continue;
								}
								for (uint32_t q : *list) {
									const real_t d = (*p_settings.guide_points)[q].distance_squared_to(p);
									if (d < best_distance) {
										best_distance = d;
										best_point = q;
									}
								}
							}
						}
					}
				}
				const int guide = best_point >= 0 ? best_point / p_settings.guide_point_count : 0;
				const float along = best_point >= 0 ? float(best_point % p_settings.guide_point_count) / (p_settings.guide_point_count - 1) : 0.0f;
				custom0.push_back(guide);
				custom0.push_back(guide);
				custom0.push_back(1.0);
				custom0.push_back(along);
			}
		}
		indices.push_back(E->value);
	}

	// Voxel flow directions change abruptly from vertex to vertex, and the anisotropic highlight then
	// draws a maze. Smooth them over the mesh: average with the neighbors (sign-aligned, a highlight
	// direction has no sign) and project back onto the surface, a few times.
	{
		const int vertex_count = vertices.size();
		LocalVector<LocalVector<int>> neighbors;
		neighbors.resize(vertex_count);
		for (int i = 0; i + 2 < indices.size(); i += 3) {
			for (int k = 0; k < 3; k++) {
				const int a = indices[i + k];
				const int b = indices[i + (k + 1) % 3];
				neighbors[a].push_back(b);
				neighbors[b].push_back(a);
			}
		}
		LocalVector<Vector3> flow;
		flow.resize(vertex_count);
		for (int v = 0; v < vertex_count; v++) {
			flow[v] = Vector3(tangents[v * 4], tangents[v * 4 + 1], tangents[v * 4 + 2]);
		}
		LocalVector<Vector3> next;
		next.resize(vertex_count);
		for (int pass = 0; pass < p_settings.flow_smoothing; pass++) {
			for (int v = 0; v < vertex_count; v++) {
				Vector3 sum = flow[v];
				for (int n : neighbors[v]) {
					sum += flow[v].dot(flow[n]) < 0 ? -flow[n] : flow[n];
				}
				const Vector3 normal = normals[v];
				sum -= normal * sum.dot(normal);
				next[v] = sum.is_zero_approx() ? flow[v] : sum.normalized();
			}
			SWAP(flow, next);
		}
		for (int v = 0; v < vertex_count; v++) {
			tangents.set(v * 4, flow[v].x);
			tangents.set(v * 4 + 1, flow[v].y);
			tangents.set(v * 4 + 2, flow[v].z);
		}
	}

	r_arrays.resize(Mesh::ARRAY_MAX);
	r_arrays[Mesh::ARRAY_VERTEX] = vertices;
	r_arrays[Mesh::ARRAY_NORMAL] = normals;
	r_arrays[Mesh::ARRAY_TANGENT] = tangents;
	r_arrays[Mesh::ARRAY_COLOR] = colors;
	r_arrays[Mesh::ARRAY_BONES] = bones;
	r_arrays[Mesh::ARRAY_WEIGHTS] = weights;
	r_arrays[Mesh::ARRAY_INDEX] = indices;
	if (guided) {
		r_arrays[Mesh::ARRAY_CUSTOM0] = custom0;
	}
	print_verbose(vformat("MetaHuman: hair shell %dx%dx%d voxels, %d triangles before simplification, %d after.", grid.size.x, grid.size.y, grid.size.z, quads_indices.size() / 3, indices.size() / 3));
	return true;
}
