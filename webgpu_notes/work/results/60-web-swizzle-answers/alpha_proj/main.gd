extends Control

# Task 60. Each 256x256 cell draws a SubViewport's texture (or a Texture2DRD) with blending over pure red.
# Natively an opaque viewport's texture reads alpha 1 (view swizzle), so no red shows through; a transparent
# one blends. Row 0: 3D opaque, 3D transparent, 2D opaque (sub + disabled blend), 2D transparent (same).
# Row 1: 2D opaque mul, 2D opaque mix only, Texture2DRD R8 through a ShaderMaterial, Texture2DRD RG8 (TextureRect).

const CELL := 256
var frames := 0
var out_path := ""
var rd_textures: Array[Texture2DRD] = []


func _cell(i: int) -> Rect2:
	return Rect2((i % 4) * CELL, (i / 4) * CELL, CELL, CELL)


func _red_bg(i: int) -> void:
	var bg := ColorRect.new()
	bg.color = Color(1, 0, 0, 1)
	bg.position = _cell(i).position
	bg.size = _cell(i).size
	add_child(bg)


func _show(i: int, tex: Texture2D, mat: Material = null) -> void:
	_red_bg(i)
	var tr := TextureRect.new()
	tr.texture = tex
	tr.position = _cell(i).position
	tr.size = _cell(i).size
	tr.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	tr.stretch_mode = TextureRect.STRETCH_SCALE
	if mat:
		tr.material = mat
	add_child(tr)


func _viewport(transparent: bool) -> SubViewport:
	var vp := SubViewport.new()
	vp.size = Vector2i(CELL, CELL)
	vp.transparent_bg = transparent
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	if ProjectSettings.get_setting("application/config/w60_msaa_hdr", false):
		# Variant: 2D MSAA (the canvas draws into the MSAA target, resolved at the end) and an HDR 2D (float) target.
		vp.msaa_2d = Viewport.MSAA_4X
		vp.msaa_3d = Viewport.MSAA_4X
		vp.use_hdr_2d = true
	add_child(vp)
	return vp


func _vp3d(transparent: bool) -> SubViewport:
	var vp := _viewport(transparent)
	var cam := Camera3D.new()
	cam.position = Vector3(0, 0, 3)
	vp.add_child(cam)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.1, 0.2, 0.8, 0.5)
	env.environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	vp.add_child(env)
	vp.own_world_3d = true
	# A half-transparent unshaded green quad over the left half.
	var mi := MeshInstance3D.new()
	var q := QuadMesh.new()
	q.size = Vector2(1.5, 3.0)
	mi.mesh = q
	mi.position = Vector3(-0.75, 0, 0)
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.albedo_color = Color(0, 1, 0, 0.5)
	mi.material_override = m
	vp.add_child(mi)
	return vp


func _rect(parent: Node, r: Rect2, c: Color, blend: int, shader_code := "") -> void:
	var cr := ColorRect.new()
	cr.position = r.position
	cr.size = r.size
	cr.color = c
	if shader_code != "":
		var sm := ShaderMaterial.new()
		sm.shader = Shader.new()
		sm.shader.code = shader_code
		cr.material = sm
	elif blend >= 0:
		var cm := CanvasItemMaterial.new()
		cm.blend_mode = blend
		cr.material = cm
	parent.add_child(cr)


func _vp2d(transparent: bool, kind: String) -> SubViewport:
	var vp := _viewport(transparent)
	vp.disable_3d = true
	match kind:
		"sub_disabled":
			# A mix rect first (keeps an opaque target at alpha 1), then subtract alpha 0.5 on the left,
			# and a blend_disabled rect writing alpha 0.25 on the bottom right.
			_rect(vp, Rect2(0, 0, CELL, CELL), Color(0.9, 0.9, 0.2, 1.0), CanvasItemMaterial.BLEND_MODE_MIX)
			_rect(vp, Rect2(0, 0, CELL / 2, CELL), Color(0.0, 0.0, 0.0, 0.5), CanvasItemMaterial.BLEND_MODE_SUB)
			_rect(vp, Rect2(CELL / 2, CELL / 2, CELL / 2, CELL / 2), Color(0.0, 0.6, 0.6, 0.25), -1,
					"shader_type canvas_item;\nrender_mode blend_disabled;\nvoid fragment() { COLOR = vec4(0.0, 0.6, 0.6, 0.25); }\n")
		"mul":
			# The clear color (alpha 0.5) on the top half, a mul rect with alpha 0.5 on the bottom half over a mix rect.
			_rect(vp, Rect2(0, CELL / 2, CELL, CELL / 2), Color(0.2, 0.8, 0.9, 1.0), CanvasItemMaterial.BLEND_MODE_MIX)
			_rect(vp, Rect2(0, CELL / 2, CELL, CELL / 2), Color(1.0, 0.5, 1.0, 0.5), CanvasItemMaterial.BLEND_MODE_MUL)
		"mix":
			# Mix only over the clear color: the clear (alpha 0.5) shows on the right half.
			_rect(vp, Rect2(0, 0, CELL / 2, CELL), Color(0.2, 0.9, 0.2, 0.6), CanvasItemMaterial.BLEND_MODE_MIX)
	return vp


func _rd_texture(fmt: int, data: PackedByteArray) -> Texture2DRD:
	var rd := RenderingServer.get_rendering_device()
	var tf := RDTextureFormat.new()
	tf.format = fmt
	tf.width = 4
	tf.height = 4
	tf.usage_bits = RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT | RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT
	var rid := rd.texture_create(tf, RDTextureView.new(), [data])
	var t := Texture2DRD.new()
	t.texture_rd_rid = rid
	rd_textures.append(t)
	return t


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out_path = a.substr(4)
	print("[ALPHA] start renderer=%s" % RenderingServer.get_current_rendering_method())
	_show(0, _vp3d(false).get_texture())
	_show(1, _vp3d(true).get_texture())
	_show(2, _vp2d(false, "sub_disabled").get_texture())
	_show(3, _vp2d(true, "sub_disabled").get_texture())
	_show(4, _vp2d(false, "mul").get_texture())
	_show(5, _vp2d(false, "mix").get_texture())
	var r8 := PackedByteArray()
	var rg8 := PackedByteArray()
	for i in 16:
		r8.append(153)
		rg8.append(153)
		rg8.append(200)
	var sm := ShaderMaterial.new()
	sm.shader = Shader.new()
	sm.shader.code = "shader_type canvas_item;\nuniform sampler2D tex : filter_nearest;\nvoid fragment() { COLOR = texture(tex, UV); }\n"
	var t_r8 := _rd_texture(RenderingDevice.DATA_FORMAT_R8_UNORM, r8)
	sm.set_shader_parameter("tex", t_r8)
	_show(6, t_r8, sm)
	_show(7, _rd_texture(RenderingDevice.DATA_FORMAT_R8G8_UNORM, rg8))


var reported := {}


func _process(_delta: float) -> void:
	frames += 1
	if frames >= 12:
		# get_image() reads back asynchronously on WebGPU (null until the map completes): retry.
		for i in rd_textures.size():
			if reported.has(i):
				continue
			var t := rd_textures[i]
			var img := t.get_image()
			if img or frames > 60:
				reported[i] = true
				print("[ALPHA] rd_texture %d rs_format=%d image=%s" % [i, RenderingServer.texture_get_format(t.get_rid()), ("format %d px %s" % [img.get_format(), img.get_pixel(0, 0)]) if img else "null"])
	if frames == 30:
		if out_path != "":
			get_viewport().get_texture().get_image().save_png(out_path)
	if frames == 62:
		print("[ALPHA] done")
		if out_path != "":
			get_tree().quit()
