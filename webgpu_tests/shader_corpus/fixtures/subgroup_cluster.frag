// Mirrors the subgroup usage of cluster_render.glsl and
// scene_forward_clustered.glsl: the ops this fork must lower for Firefox, whose
// WGSL parser rejects `enable subgroups` outright (TASKS.md Task 38).
#version 450

#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable
#extension GL_KHR_shader_subgroup_vote : enable

layout(set = 0, binding = 0, std430) buffer ClusterBuffer {
	uint data[];
}
cluster_render;

layout(location = 0) flat in uint cluster_offset;
layout(location = 1) flat in uint item_from_in;
layout(location = 2) flat in uint item_to_in;
layout(location = 0) out vec4 frag_color;

void main() {
	// cluster_render.glsl: elect one lane per distinct cluster offset.
	uint cluster_thread_group_index;
	uvec4 mask;
	while (true) {
		uint first = subgroupBroadcastFirst(cluster_offset);
		mask = subgroupBallot(first == cluster_offset);
		if (first == cluster_offset) {
			break;
		}
	}
	cluster_thread_group_index = subgroupBallotExclusiveBitCount(mask);

	uint aux = 0;
	if (cluster_thread_group_index == 0) {
		aux = atomicOr(cluster_render.data[cluster_offset], 1u);
	}

	// cluster_render.glsl: merge the depth bits across the subgroup.
	uint z_write_bit = 1u << (cluster_offset & 31u);
	z_write_bit = subgroupOr(z_write_bit);
	if (cluster_thread_group_index == 0) {
		aux |= atomicOr(cluster_render.data[cluster_offset + 1u], z_write_bit);
	}

	// scene_forward_clustered.glsl: widen the iteration range to the subgroup.
	uint item_from = subgroupBroadcastFirst(subgroupMin(item_from_in));
	uint item_to = subgroupBroadcastFirst(subgroupMax(item_to_in));
	uint acc = 0;
	for (uint i = item_from; i < item_to && i < 8u; i++) {
		uint m = cluster_render.data[i];
		uint merged_mask = subgroupBroadcastFirst(subgroupOr(m));
		acc += merged_mask & m;
	}

	frag_color = vec4(float(aux), float(acc), float(item_from), float(item_to));
}
