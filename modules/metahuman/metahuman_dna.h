/**************************************************************************/
/*  metahuman_dna.h                                                       */
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

#include "core/io/resource.h"
#include "core/math/quaternion.h"
#include "core/math/vector3.h"

namespace rl4 {
class RigLogic;
} // namespace rl4

namespace dna {
class BinaryStreamReader;
} // namespace dna

// A MetaHuman rig: the behavior layers of a DNA file (controls, joints, correctives), without the
// geometry, which the importer bakes into meshes. RigLogic compiles it once; every MetaHumanRig
// using this resource shares that and keeps only its own per-instance state.
class MetaHumanDNA : public Resource {
	GDCLASS(MetaHumanDNA, Resource);

public:
	// DNA translations are centimeters; Godot's are meters. The axes already agree (Y up, +Z front).
	static constexpr real_t DNA_TO_GODOT_SCALE = 0.01;

	struct Joint {
		String name;
		int parent = -1;
		// Local neutral pose, in Godot units.
		Vector3 translation;
		Quaternion rotation;
		Vector3 scale = Vector3(1, 1, 1);
	};

private:
	PackedByteArray data;

	dna::BinaryStreamReader *reader = nullptr;
	rl4::RigLogic *riglogic = nullptr;
	bool load_attempted = false;

	Vector<Joint> joints;
	PackedStringArray gui_control_names;
	PackedStringArray raw_control_names;
	PackedStringArray animated_map_names;
	int lod_count = 0;

	void _clear();
	void _load() const;

protected:
	static void _bind_methods();

public:
	// Strips a full DNA file down to what the rig needs at run time.
	static PackedByteArray strip_geometry(const PackedByteArray &p_full_dna);

	void set_data(const PackedByteArray &p_data);
	PackedByteArray get_data() const;

	bool is_valid() const;
	rl4::RigLogic *get_riglogic() const;
	const dna::BinaryStreamReader *get_reader() const;

	const Vector<Joint> &get_joints() const;
	int get_joint_count() const;
	String get_joint_name(int p_joint) const;
	int get_lod_count() const;
	PackedStringArray get_gui_control_names() const;
	PackedStringArray get_raw_control_names() const;
	PackedStringArray get_animated_map_names() const;

	~MetaHumanDNA();
};
