/**************************************************************************/
/*  resource_importer_cube_lut.cpp                                        */
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

#include "resource_importer_cube_lut.h"

#include "core/io/file_access.h"
#include "editor/import/resource_importer_layered_texture.h"
#include "editor/import/resource_importer_texture.h"
#include "scene/resources/compressed_texture.h"

// The grid a table without its own 3D grid is resampled to: a 1D table, or a 3D one whose domain is not 0-1.
static constexpr int CUBE_LUT_RESAMPLE_SIZE = 65;
// The largest 3D table accepted (256^3 RGBA half floats is 128 MiB).
static constexpr int CUBE_LUT_MAX_3D_SIZE = 256;
static constexpr int CUBE_LUT_MAX_1D_SIZE = 65536;

String ResourceImporterCubeLUT::get_importer_name() const {
	return "cube_lut";
}

String ResourceImporterCubeLUT::get_visible_name() const {
	return "LUT (.cube)";
}

void ResourceImporterCubeLUT::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("cube");
}

String ResourceImporterCubeLUT::get_save_extension() const {
	return "ctex3d";
}

String ResourceImporterCubeLUT::get_resource_type() const {
	return "CompressedTexture3D";
}

int ResourceImporterCubeLUT::get_preset_count() const {
	return 0;
}

String ResourceImporterCubeLUT::get_preset_name(int p_idx) const {
	return String();
}

void ResourceImporterCubeLUT::get_import_options(const String &p_path, List<ImportOption> *r_options, int p_preset) const {
}

bool ResourceImporterCubeLUT::get_option_visibility(const String &p_path, const String &p_option, const HashMap<StringName, Variant> &p_options) const {
	return true;
}

static float _cube_lut_lerp_1d(const Vector<Color> &p_table, int p_channel, float p_t) {
	const int last = p_table.size() - 1;
	const float x = CLAMP(p_t, 0.0f, 1.0f) * last;
	const int i = MIN(int(x), last - 1);
	const float f = x - i;
	return Math::lerp(p_table[i][p_channel], p_table[i + 1][p_channel], f);
}

static Color _cube_lut_trilinear(const Vector<Color> &p_table, int p_size, const Vector3 &p_t) {
	const int last = p_size - 1;
	int i[3];
	float f[3];
	for (int c = 0; c < 3; c++) {
		const float x = CLAMP(p_t[c], 0.0f, 1.0f) * last;
		i[c] = MIN(int(x), last - 1);
		f[c] = x - i[c];
	}
	Color result;
	for (int corner = 0; corner < 8; corner++) {
		const int dr = corner & 1, dg = (corner >> 1) & 1, db = (corner >> 2) & 1;
		const float w = (dr ? f[0] : 1.0f - f[0]) * (dg ? f[1] : 1.0f - f[1]) * (db ? f[2] : 1.0f - f[2]);
		const Color &v = p_table[(i[2] + db) * p_size * p_size + (i[1] + dg) * p_size + (i[0] + dr)];
		result.r += v.r * w;
		result.g += v.g * w;
		result.b += v.b * w;
	}
	result.a = 1.0;
	return result;
}

Error ResourceImporterCubeLUT::parse_cube(const String &p_text, int &r_size, Vector<Color> &r_table, String &r_error) {
	int size_1d = 0;
	int size_3d = 0;
	// DOMAIN_MIN/MAX belong to whichever table the file has (to the 1D table if it has both); Resolve's
	// LUT_1D_INPUT_RANGE / LUT_3D_INPUT_RANGE name their table.
	Vector3 domain_min(0, 0, 0), domain_max(1, 1, 1);
	Vector3 range_1d_min(0, 0, 0), range_1d_max(1, 1, 1);
	Vector3 range_3d_min(0, 0, 0), range_3d_max(1, 1, 1);
	bool has_domain = false, has_range_1d = false, has_range_3d = false;
	Vector<Color> data;

	const Vector<String> lines = p_text.split("\n");
	for (int l = 0; l < lines.size(); l++) {
		const String line = lines[l].strip_edges();
		if (line.is_empty() || line.begins_with("#")) {
			continue;
		}
		const Vector<String> tokens = line.split_spaces();
		const String &key = tokens[0];
		if (key == "TITLE" || key == "LUT_IN_VIDEO_RANGE" || key == "LUT_OUT_VIDEO_RANGE") {
			continue;
		} else if (key == "LUT_1D_SIZE" || key == "LUT_3D_SIZE") {
			if (tokens.size() != 2 || !tokens[1].is_valid_int()) {
				r_error = vformat("Line %d: %s needs one integer.", l + 1, key);
				return ERR_PARSE_ERROR;
			}
			const int n = tokens[1].to_int();
			const int max_n = key == "LUT_1D_SIZE" ? CUBE_LUT_MAX_1D_SIZE : CUBE_LUT_MAX_3D_SIZE;
			if (n < 2 || n > max_n) {
				r_error = vformat("Line %d: %s must be between 2 and %d.", l + 1, key, max_n);
				return ERR_PARSE_ERROR;
			}
			(key == "LUT_1D_SIZE" ? size_1d : size_3d) = n;
		} else if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
			if (tokens.size() != 4) {
				r_error = vformat("Line %d: %s needs three numbers.", l + 1, key);
				return ERR_PARSE_ERROR;
			}
			const Vector3 v(tokens[1].to_float(), tokens[2].to_float(), tokens[3].to_float());
			(key == "DOMAIN_MIN" ? domain_min : domain_max) = v;
			has_domain = true;
		} else if (key == "LUT_1D_INPUT_RANGE" || key == "LUT_3D_INPUT_RANGE") {
			if (tokens.size() != 3) {
				r_error = vformat("Line %d: %s needs two numbers.", l + 1, key);
				return ERR_PARSE_ERROR;
			}
			const float lo = tokens[1].to_float();
			const float hi = tokens[2].to_float();
			if (key == "LUT_1D_INPUT_RANGE") {
				range_1d_min = Vector3(lo, lo, lo);
				range_1d_max = Vector3(hi, hi, hi);
				has_range_1d = true;
			} else {
				range_3d_min = Vector3(lo, lo, lo);
				range_3d_max = Vector3(hi, hi, hi);
				has_range_3d = true;
			}
		} else if (key.is_valid_float()) {
			if (tokens.size() != 3 || !tokens[1].is_valid_float() || !tokens[2].is_valid_float()) {
				r_error = vformat("Line %d: a table entry needs three numbers.", l + 1);
				return ERR_PARSE_ERROR;
			}
			data.push_back(Color(tokens[0].to_float(), tokens[1].to_float(), tokens[2].to_float()));
		} else {
			r_error = vformat("Line %d: unknown keyword \"%s\".", l + 1, key);
			return ERR_PARSE_ERROR;
		}
	}

	if (size_1d == 0 && size_3d == 0) {
		r_error = "No LUT_1D_SIZE or LUT_3D_SIZE.";
		return ERR_PARSE_ERROR;
	}
	const int expected = size_1d + size_3d * size_3d * size_3d;
	if (data.size() != expected) {
		r_error = vformat("Expected %d table entries, found %d.", expected, data.size());
		return ERR_PARSE_ERROR;
	}

	if (has_domain) {
		if (size_1d > 0 && !has_range_1d) {
			range_1d_min = domain_min;
			range_1d_max = domain_max;
		} else if (size_3d > 0 && !has_range_3d) {
			range_3d_min = domain_min;
			range_3d_max = domain_max;
		}
	}
	for (int c = 0; c < 3; c++) {
		if (range_1d_max[c] <= range_1d_min[c] || range_3d_max[c] <= range_3d_min[c]) {
			r_error = "A domain's maximum must be greater than its minimum.";
			return ERR_PARSE_ERROR;
		}
	}

	const Vector<Color> table_1d = data.slice(0, size_1d);
	const Vector<Color> table_3d = data.slice(size_1d);
	const bool default_3d_domain = range_3d_min == Vector3(0, 0, 0) && range_3d_max == Vector3(1, 1, 1);

	if (size_1d == 0 && default_3d_domain) {
		// A plain 3D table over 0-1 is used as it is.
		r_size = size_3d;
		r_table = table_3d;
		return OK;
	}

	// Otherwise evaluate the file's tables over a grid on the 0-1 input cube: input outside a table's domain clamps
	// to its edge, as in grading tools.
	r_size = size_3d > 0 ? MAX(size_3d, 33) : CUBE_LUT_RESAMPLE_SIZE;
	r_table.resize(r_size * r_size * r_size);
	Color *w = r_table.ptrw();
	for (int b = 0; b < r_size; b++) {
		for (int g = 0; g < r_size; g++) {
			for (int r = 0; r < r_size; r++) {
				Vector3 v(float(r) / (r_size - 1), float(g) / (r_size - 1), float(b) / (r_size - 1));
				if (size_1d > 0) {
					for (int c = 0; c < 3; c++) {
						v[c] = _cube_lut_lerp_1d(table_1d, c, (v[c] - range_1d_min[c]) / (range_1d_max[c] - range_1d_min[c]));
					}
				}
				Color out(v.x, v.y, v.z);
				if (size_3d > 0) {
					out = _cube_lut_trilinear(table_3d, size_3d, (v - range_3d_min) / (range_3d_max - range_3d_min));
				}
				w[(b * r_size + g) * r_size + r] = out;
			}
		}
	}
	return OK;
}

Error ResourceImporterCubeLUT::import(ResourceUID::ID p_source_id, const String &p_source_file, const String &p_save_path, const HashMap<StringName, Variant> &p_options, List<String> *r_platform_variants, List<String> *r_gen_files, Variant *r_metadata) {
	Error err;
	const String text = FileAccess::get_file_as_string(p_source_file, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Cannot open LUT file \"%s\".", p_source_file));

	int size = 0;
	Vector<Color> table;
	String message;
	err = parse_cube(text, size, table, message);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Cannot import LUT \"%s\": %s", p_source_file, message));

	// Written as the 3D texture importer writes one (CompressedTexture3D), uncompressed, without going through the
	// RenderingServer: an ImageTexture3D saves by reading its data back from the GPU, which a headless import does not
	// have. Half floats, because grading LUTs can leave 0-1 and 8 bits would band a smooth curve.
	const String save_path = p_save_path + "." + get_save_extension();
	Ref<FileAccess> f = FileAccess::open(save_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, vformat("Cannot create \"%s\".", save_path));
	f->store_8('G');
	f->store_8('S');
	f->store_8('T');
	f->store_8('L');
	f->store_32(CompressedTextureLayered::FORMAT_VERSION);
	f->store_32(size); // Depth.
	f->store_32(ResourceImporterLayeredTexture::MODE_3D);
	f->store_32(0);
	f->store_32(0);
	f->store_32(0); // No mipmaps.
	f->store_32(0);
	f->store_32(0);

	for (int b = 0; b < size; b++) {
		Ref<Image> slice = Image::create_empty(size, size, false, Image::FORMAT_RGBAH);
		for (int g = 0; g < size; g++) {
			for (int r = 0; r < size; r++) {
				slice->set_pixel(r, g, table[(b * size + g) * size + r]);
			}
		}
		ResourceImporterTexture::save_to_ctex_format(f, slice, ResourceImporterTexture::COMPRESS_VRAM_UNCOMPRESSED, Image::USED_CHANNELS_RGBA, Image::COMPRESS_S3TC, Image::COMPRESS_PROFILE_AUTOMATIC, 1.0, Image::BasisUniversalPackerParams(), Image::BPTC_DETECT);
	}
	return OK;
}
