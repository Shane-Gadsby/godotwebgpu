/**************************************************************************/
/*  metahuman_groom.cpp                                                   */
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

#include "metahuman_groom.h"

#include "core/io/file_access.h"

// Just enough of Alembic's Ogawa container to read one groom: a tree of groups (lists of child
// offsets) and data blocks; on top of it, Alembic objects (group: properties, children, child
// headers) and properties (compound groups ending in a header block; arrays as (data, dims) block
// pairs per sample). Only the first sample of each property is read: grooms are static.

namespace {

constexpr uint64_t OGAWA_DATA_FLAG = 0x8000000000000000ull;

class OgawaReader {
	Ref<FileAccess> f;

public:
	uint64_t root = 0;

	Error open(const String &p_path) {
		f = FileAccess::open(p_path, FileAccess::READ);
		ERR_FAIL_COND_V_MSG(f.is_null(), ERR_FILE_CANT_OPEN, vformat("MetaHuman: could not open groom '%s'.", p_path));
		uint8_t magic[5] = {};
		f->get_buffer(magic, 5);
		ERR_FAIL_COND_V_MSG(memcmp(magic, "Ogawa", 5) != 0, ERR_FILE_UNRECOGNIZED, vformat("MetaHuman: '%s' is not an Ogawa Alembic file (HDF5 Alembic is not supported).", p_path));
		f->seek(8);
		root = f->get_64();
		return OK;
	}

	LocalVector<uint64_t> group(uint64_t p_pos) {
		LocalVector<uint64_t> kids;
		if (p_pos == 0 || (p_pos & OGAWA_DATA_FLAG)) {
			return kids;
		}
		f->seek(p_pos);
		const uint64_t count = f->get_64();
		ERR_FAIL_COND_V(count > (1u << 24), kids);
		kids.resize(count);
		for (uint64_t i = 0; i < count; i++) {
			kids[i] = f->get_64();
		}
		return kids;
	}

	uint64_t data_size(uint64_t p_ref) {
		const uint64_t pos = p_ref & ~OGAWA_DATA_FLAG;
		if (pos == 0) {
			return 0;
		}
		f->seek(pos);
		return f->get_64();
	}

	// Reads a data block, skipping p_skip bytes (array samples start with a 16-byte digest).
	Vector<uint8_t> data(uint64_t p_ref, uint64_t p_skip = 0) {
		Vector<uint8_t> bytes;
		const uint64_t size = data_size(p_ref);
		if (size <= p_skip) {
			return bytes;
		}
		f->seek((p_ref & ~OGAWA_DATA_FLAG) + 8 + p_skip);
		bytes.resize(size - p_skip);
		f->get_buffer(bytes.ptrw(), bytes.size());
		return bytes;
	}
};

struct PropertyHeader {
	String name;
	int type = 0; // 0 compound, 1 scalar, 2 array.
	int pod = 0;
	uint64_t ref = 0;
};

struct Archive {
	OgawaReader ogawa;
	Vector<String> indexed_metadata;
};

uint32_t _read_sized(const uint8_t *p_buf, int p_size, int p_hint, int &r_pos) {
	uint32_t v = 0;
	if (p_hint == 0 && r_pos + 1 <= p_size) {
		v = p_buf[r_pos];
		r_pos += 1;
	} else if (p_hint == 1 && r_pos + 2 <= p_size) {
		v = p_buf[r_pos] | (p_buf[r_pos + 1] << 8);
		r_pos += 2;
	} else if (r_pos + 4 <= p_size) {
		memcpy(&v, p_buf + r_pos, 4);
		r_pos += 4;
	}
	return v;
}

Vector<PropertyHeader> _property_headers(Archive &p_archive, uint64_t p_compound) {
	Vector<PropertyHeader> headers;
	const LocalVector<uint64_t> kids = p_archive.ogawa.group(p_compound);
	if (kids.is_empty()) {
		return headers;
	}
	const Vector<uint8_t> buf = p_archive.ogawa.data(kids[kids.size() - 1]);
	const uint8_t *b = buf.ptr();
	const int size = buf.size();
	int pos = 0;
	while (pos + 4 <= size) {
		uint32_t info;
		memcpy(&info, b + pos, 4);
		pos += 4;
		PropertyHeader h;
		h.type = info & 3;
		const int hint = (info >> 2) & 3;
		h.pod = (info >> 4) & 0xF;
		const uint32_t metadata_index = (info >> 20) & 0xFF;
		if (h.type != 0) {
			_read_sized(b, size, hint, pos); // Sample count.
			if (info & 0x200) {
				_read_sized(b, size, hint, pos); // First changed index.
				_read_sized(b, size, hint, pos); // Last changed index.
			}
			if (info & 0x100) {
				_read_sized(b, size, hint, pos); // Time sampling index.
			}
		}
		const uint32_t name_size = _read_sized(b, size, hint, pos);
		ERR_BREAK(pos + (int)name_size > size);
		h.name = String::utf8((const char *)b + pos, name_size);
		pos += name_size;
		if (metadata_index == 0xFF) {
			const uint32_t metadata_size = _read_sized(b, size, hint, pos);
			pos += metadata_size;
		}
		const uint32_t index = headers.size();
		h.ref = index < kids.size() - 1 ? kids[index] : 0;
		headers.push_back(h);
	}
	return headers;
}

uint64_t _find_property(Archive &p_archive, uint64_t p_compound, const String &p_name) {
	for (const PropertyHeader &h : _property_headers(p_archive, p_compound)) {
		if (h.name == p_name) {
			return h.ref;
		}
	}
	return 0;
}

// The first sample of an array property, as raw bytes.
Vector<uint8_t> _array_sample(Archive &p_archive, uint64_t p_property) {
	if (p_property == 0) {
		return Vector<uint8_t>();
	}
	const LocalVector<uint64_t> samples = p_archive.ogawa.group(p_property);
	if (samples.is_empty()) {
		return Vector<uint8_t>();
	}
	return p_archive.ogawa.data(samples[0], 16);
}

template <typename T>
LocalVector<T> _array_as(Archive &p_archive, uint64_t p_property) {
	const Vector<uint8_t> bytes = _array_sample(p_archive, p_property);
	LocalVector<T> out;
	out.resize(bytes.size() / sizeof(T));
	if (!out.is_empty()) {
		memcpy(out.ptr(), bytes.ptr(), out.size() * sizeof(T));
	}
	return out;
}

// Object header blocks: name, then metadata (inline when its index byte is 0xFF), and a 32-byte
// digest at the end.
struct ObjectHeader {
	String name;
	String metadata;
};

Vector<ObjectHeader> _object_headers(Archive &p_archive, uint64_t p_ref) {
	Vector<ObjectHeader> headers;
	const Vector<uint8_t> buf = p_archive.ogawa.data(p_ref);
	const uint8_t *b = buf.ptr();
	const int end = buf.size() - 32;
	int pos = 0;
	while (pos + 4 <= end) {
		uint32_t name_size;
		memcpy(&name_size, b + pos, 4);
		pos += 4;
		ERR_BREAK(pos + (int)name_size + 1 > end + 32);
		ObjectHeader h;
		h.name = String::utf8((const char *)b + pos, name_size);
		pos += name_size;
		const uint8_t index = b[pos];
		pos += 1;
		if (index == 0xFF) {
			uint32_t metadata_size;
			memcpy(&metadata_size, b + pos, 4);
			pos += 4;
			h.metadata = String::utf8((const char *)b + pos, metadata_size);
			pos += metadata_size;
		} else if (index < p_archive.indexed_metadata.size()) {
			h.metadata = p_archive.indexed_metadata[index];
		}
		headers.push_back(h);
	}
	return headers;
}

// Depth-first search for the first curves object; returns its properties compound.
uint64_t _find_curves(Archive &p_archive, uint64_t p_object, int p_depth) {
	ERR_FAIL_COND_V(p_depth > 32, 0);
	const LocalVector<uint64_t> kids = p_archive.ogawa.group(p_object);
	if (kids.size() < 2 || !(kids[kids.size() - 1] & OGAWA_DATA_FLAG)) {
		return 0;
	}
	const Vector<ObjectHeader> children = _object_headers(p_archive, kids[kids.size() - 1]);
	for (int i = 0; i < children.size() && i + 1 < (int)kids.size() - 1; i++) {
		const LocalVector<uint64_t> child = p_archive.ogawa.group(kids[i + 1]);
		if (children[i].metadata.contains("AbcGeom_Curve") && !child.is_empty()) {
			return child[0];
		}
		const uint64_t found = _find_curves(p_archive, kids[i + 1], p_depth + 1);
		if (found) {
			return found;
		}
	}
	return 0;
}

} // namespace

Error MetaHumanGroom::load(const String &p_path, MetaHumanGroom &r_groom) {
	Archive archive;
	Error err = archive.ogawa.open(p_path);
	if (err != OK) {
		return err;
	}
	const LocalVector<uint64_t> top = archive.ogawa.group(archive.ogawa.root);
	ERR_FAIL_COND_V_MSG(top.size() < 3, ERR_FILE_CORRUPT, vformat("MetaHuman: '%s' has no Alembic archive.", p_path));
	if (top.size() > 5) {
		// Indexed metadata: entry 0 is empty, then length-prefixed strings.
		const Vector<uint8_t> buf = archive.ogawa.data(top[5]);
		archive.indexed_metadata.push_back(String());
		int pos = 0;
		while (pos < buf.size()) {
			const int length = buf[pos++];
			archive.indexed_metadata.push_back(String::utf8((const char *)buf.ptr() + pos, MIN(length, buf.size() - pos)));
			pos += length;
		}
	}

	const uint64_t properties = _find_curves(archive, top[2], 0);
	ERR_FAIL_COND_V_MSG(properties == 0, ERR_FILE_CORRUPT, vformat("MetaHuman: '%s' has no curves.", p_path));
	const uint64_t geom = _find_property(archive, properties, ".geom");
	ERR_FAIL_COND_V(geom == 0, ERR_FILE_CORRUPT);
	const uint64_t arb = _find_property(archive, geom, ".arbGeomParams");

	const LocalVector<float> positions = _array_as<float>(archive, _find_property(archive, geom, "P"));
	const LocalVector<int32_t> counts = _array_as<int32_t>(archive, _find_property(archive, geom, "nVertices"));
	ERR_FAIL_COND_V_MSG(positions.is_empty() || counts.is_empty(), ERR_FILE_CORRUPT, vformat("MetaHuman: '%s' has no curve points.", p_path));

	const uint32_t point_count = positions.size() / 3;
	r_groom.points.resize(point_count);
	for (uint32_t i = 0; i < point_count; i++) {
		r_groom.points[i] = to_godot(positions.ptr() + i * 3);
	}
	r_groom.curve_offsets.resize(counts.size() + 1);
	uint32_t offset = 0;
	for (uint32_t c = 0; c < counts.size(); c++) {
		r_groom.curve_offsets[c] = offset;
		offset += MAX(counts[c], 0);
	}
	r_groom.curve_offsets[counts.size()] = offset;
	ERR_FAIL_COND_V_MSG(offset != point_count, ERR_FILE_CORRUPT, vformat("MetaHuman: '%s' curve sizes do not add up to its points.", p_path));

	r_groom.widths = _array_as<float>(archive, _find_property(archive, geom, "width"));
	if (r_groom.widths.size() == point_count) {
		for (float &w : r_groom.widths) {
			w *= 0.01;
		}
	} else {
		r_groom.widths.clear();
	}
	const LocalVector<float> uvs = _array_as<float>(archive, _find_property(archive, geom, "uv"));
	if (uvs.size() == counts.size() * 2) {
		r_groom.root_uvs.resize(counts.size());
		for (uint32_t c = 0; c < counts.size(); c++) {
			r_groom.root_uvs[c] = Vector2(uvs[c * 2], uvs[c * 2 + 1]);
		}
	}

	if (arb) {
		const LocalVector<float> colors = _array_as<float>(archive, _find_property(archive, arb, "groom_color"));
		if (colors.size() == point_count * 3) {
			r_groom.colors.resize(point_count);
			for (uint32_t i = 0; i < point_count; i++) {
				r_groom.colors[i] = Color(colors[i * 3], colors[i * 3 + 1], colors[i * 3 + 2]);
			}
		}
		const LocalVector<int32_t> guides = _array_as<int32_t>(archive, _find_property(archive, arb, "groom_guide"));
		r_groom.is_guide.resize(counts.size());
		for (uint32_t c = 0; c < counts.size(); c++) {
			r_groom.is_guide[c] = c < guides.size() && guides[c] != 0;
		}
		r_groom.sim_curve_a = _array_as<int32_t>(archive, _find_property(archive, arb, "groom_sim_curve_a"));
		r_groom.sim_curve_b = _array_as<int32_t>(archive, _find_property(archive, arb, "groom_sim_curve_b"));
		r_groom.sim_weight_a = _array_as<float>(archive, _find_property(archive, arb, "groom_sim_weight_a"));
		r_groom.sim_weight_b = _array_as<float>(archive, _find_property(archive, arb, "groom_sim_weight_b"));
		r_groom.sim_vertex_a = _array_as<int32_t>(archive, _find_property(archive, arb, "groom_sim_vertex_a"));
		r_groom.sim_vertex_b = _array_as<int32_t>(archive, _find_property(archive, arb, "groom_sim_vertex_b"));
		r_groom.sim_lerp_a = _array_as<float>(archive, _find_property(archive, arb, "groom_sim_lerp_a"));
		r_groom.sim_lerp_b = _array_as<float>(archive, _find_property(archive, arb, "groom_sim_lerp_b"));
	} else {
		r_groom.is_guide.resize(counts.size());
		for (uint8_t &g : r_groom.is_guide) {
			g = 0;
		}
	}
	return OK;
}
