extends RefCounted
# The chart of grade_test.gd (copied).

static func make_chart(chart: String) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var q := QuadMesh.new()
	q.size = Vector2(1, 1)
	mi.mesh = q
	var sh := Shader.new()
	sh.code = """
shader_type spatial;
render_mode unshaded;
uniform int levels;
uniform int side;
uniform float offset;
void fragment() {
	ivec2 cell = ivec2(floor(UV * float(side)));
	int idx = cell.y * side + cell.x;
	vec3 c = vec3(float(idx % levels), float((idx / levels) % levels), float(idx / (levels * levels)));
	c = (c + offset) / 8.0;
	if (idx >= levels * levels * levels) {
		c = vec3(0.0);
	}
	// sRGB-encoded c, in linear light: the linear tonemapper and the output encoding give c back.
	ALBEDO = mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, lessThan(c, vec3(0.04045)));
}
"""
	var m := ShaderMaterial.new()
	m.shader = sh
	if chart == "off":
		m.set_shader_parameter("levels", 8)
		m.set_shader_parameter("side", 23)
		m.set_shader_parameter("offset", 0.5)
	else:
		m.set_shader_parameter("levels", 9)
		m.set_shader_parameter("side", 27)
		m.set_shader_parameter("offset", 0.0)
	mi.material_override = m
	return mi
