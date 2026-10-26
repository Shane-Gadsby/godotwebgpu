extends SceneTree
# Measures world size per UV unit on the LOD0 head/body skin surfaces: sqrt(3D area / UV area), per triangle,
# reported as median and percentiles over the area-weighted triangles.
func _initialize() -> void:
	var ava: Node3D = (load("res://MH/head.dna") as PackedScene).instantiate()
	root.add_child(ava)
	for mi in ava.find_children("*", "MeshInstance3D", true, false):
		var m: Mesh = mi.mesh
		if m == null or not (str(mi.name).contains("lod0") or str(mi.name).to_lower().contains("body")):
			continue
		for s in m.get_surface_count():
			var mat := m.surface_get_material(s)
			if mi.get_surface_override_material(s): mat = mi.get_surface_override_material(s)
			var mname := mat.resource_name if mat else ""
			if not (mname.contains("Skin") or mname.contains("Body")):
				continue
			var arr := m.surface_get_arrays(s)
			var v: PackedVector3Array = arr[Mesh.ARRAY_VERTEX]
			var uv: PackedVector2Array = arr[Mesh.ARRAY_TEX_UV]
			var idx: PackedInt32Array = arr[Mesh.ARRAY_INDEX]
			var ratios := []
			var a3 := 0.0
			var a2 := 0.0
			for t in range(0, idx.size(), 3):
				var p0 := v[idx[t]]; var p1 := v[idx[t+1]]; var p2 := v[idx[t+2]]
				var u0 := uv[idx[t]]; var u1 := uv[idx[t+1]]; var u2 := uv[idx[t+2]]
				var A := (p1 - p0).cross(p2 - p0).length() * 0.5
				var B := absf((u1 - u0).cross(u2 - u0)) * 0.5
				if B > 1e-12 and A > 0.0:
					ratios.append(sqrt(A / B))
					a3 += A; a2 += B
			ratios.sort()
			var n := ratios.size()
			print("%s surf %d %s: tris %d, overall %.1f cm/UV, p10 %.1f p50 %.1f p90 %.1f cm/UV, UV area %.3f" % [mi.name, s, mname, n, sqrt(a3/a2)*100.0, ratios[n/10]*100.0, ratios[n/2]*100.0, ratios[n*9/10]*100.0, a2])
	quit()
