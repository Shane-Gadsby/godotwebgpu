extends Node
# Texture2D.get_image_async() / RenderingDevice.*_get_data_async() checks (work task 66), and their awaitable forms
# Texture2D.get_image_awaitable() / RenderingServer.texture_2d_get_awaitable() (work task 71). Runs the same natively and
# on the web; prints one "W66RESULT <json>" line and "W66DONE". The synchronous reads at frames 5-8 are also what the
# runners count the web's debug-only "returned null for" warning on (one per texture: w96, w333; none for the invalid
# RID or the texture without copy-from usage). User args (after --): mode=test (default),
# mode=perf_none or mode=perf_readback (a readback of the main viewport every frame, for gpu_time.mjs).

const PATTERN_SHADER := """
shader_type canvas_item;
render_mode unshaded;
uniform int frame_id;
uniform int seed;
void fragment() {
	ivec2 p = ivec2(FRAGCOORD.xy);
	COLOR = vec4(float((p.x * 7 + p.y * 13 + seed) % 256) / 255.0,
			float((p.x + p.y * 3 + frame_id * 5) % 256) / 255.0,
			float((p.y * 29 + seed * 3) % 256) / 255.0, 1.0);
}
"""

const WIDTHS := [1, 3, 96, 333]
const HEIGHT := 7

var mode := "test"
var frame := 0
var results := {}
var pending := {} # request id -> {name, expect, requested_frame}
var callbacks := {} # request id -> number of times called
var order_log := []
var next_id := 0
var shader: Shader
var vps := {} # name -> {vp, mat, seed, frame_id}
var y_flip := -1 # -1 unknown, 0 no, 1 yes; decided by the first pattern image, then held to.
var done := false
var perf_count := 0
var perf_t0 := 0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("mode="):
			mode = a.substr(5)
	print("W66 platform=%s method=%s driver=%s mode=%s debug=%s" % [OS.get_name(), RenderingServer.get_current_rendering_method(), RenderingServer.get_current_rendering_driver_name(), mode, OS.is_debug_build()])
	shader = Shader.new()
	shader.code = PATTERN_SHADER
	for w in WIDTHS:
		_add_pattern_vp("w%d" % w, Vector2i(w, HEIGHT), w)
	_add_pattern_vp("big", Vector2i(256, 64), 5)
	_add_pattern_vp("hdr", Vector2i(333, 7), 9)
	vps["hdr"].vp.use_hdr_2d = true
	_add_black_vps()
	if mode.begins_with("perf"):
		perf_t0 = Time.get_ticks_msec()
		return
	# First frame: the request is issued once this frame has drawn, so it sees frame 0's pattern.
	for w in WIDTHS:
		_request("first_w%d" % w, "w%d" % w)
	_request("first_big", "big")
	_request_hdr("first_hdr2d_w333")
	_request_black()


func _add_pattern_vp(p_name: String, p_size: Vector2i, p_seed: int) -> void:
	var vp := SubViewport.new()
	vp.size = p_size
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	vp.transparent_bg = false
	var rect := ColorRect.new()
	rect.size = Vector2(vp.size) # A SubViewport can be wider than asked (1 px wide is 2).
	var mat := ShaderMaterial.new()
	mat.shader = shader
	mat.set_shader_parameter("seed", p_seed)
	mat.set_shader_parameter("frame_id", 0)
	rect.material = mat
	vp.add_child(rect)
	add_child(vp)
	vps[p_name] = { "vp": vp, "mat": mat, "seed": p_seed, "frame_id": 0 }


func _add_black_vps() -> void:
	# Fully transparent (every byte 0) and opaque black: real frames that look like "nothing arrived".
	var clear := SubViewport.new()
	clear.size = Vector2i(33, 5)
	clear.transparent_bg = true
	clear.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(clear)
	vps["clear"] = { "vp": clear }
	var black := SubViewport.new()
	black.size = Vector2i(33, 5)
	black.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	var rect := ColorRect.new()
	rect.size = Vector2(33, 5)
	rect.color = Color(0, 0, 0, 1)
	black.add_child(rect)
	add_child(black)
	vps["black"] = { "vp": black }


func _set_frame_id(p_name: String, p_frame_id: int) -> void:
	vps[p_name].frame_id = p_frame_id
	vps[p_name].mat.set_shader_parameter("frame_id", p_frame_id)


func _expected_pattern(p_name: String) -> Dictionary:
	var v: Dictionary = vps[p_name]
	return { "kind": "pattern", "seed": v.seed, "frame_id": v.frame_id, "size": v.vp.size }


func _request(p_test: String, p_vp: String) -> void:
	var id := _new_id(p_test, _expected_pattern(p_vp))
	vps[p_vp].vp.get_texture().get_image_async(_on_image.bind(id))


func _request_hdr(p_test: String) -> void:
	var e := _expected_pattern("hdr")
	e.kind = "pattern_hdr"
	var id := _new_id(p_test, e)
	vps["hdr"].vp.get_texture().get_image_async(_on_image.bind(id))


func _request_black() -> void:
	var id := _new_id("black_transparent", { "kind": "fill", "color": [0, 0, 0, 0], "size": Vector2i(33, 5) })
	vps["clear"].vp.get_texture().get_image_async(_on_image.bind(id))
	id = _new_id("black_opaque", { "kind": "fill", "color": [0, 0, 0, 255], "size": Vector2i(33, 5) })
	vps["black"].vp.get_texture().get_image_async(_on_image.bind(id))


func _new_id(p_test: String, p_expect: Dictionary) -> int:
	var id := next_id
	next_id += 1
	pending[id] = { "test": p_test, "expect": p_expect, "frame": frame }
	callbacks[id] = 0
	return id


func _check_pattern(img: Image, e: Dictionary, flip: bool, tolerance := 0) -> int:
	var size: Vector2i = e.size
	var bad := 0
	for y in size.y:
		var py := (size.y - 1 - y) if flip else y
		for x in size.x:
			var c := img.get_pixel(x, y)
			var r: int = (x * 7 + py * 13 + e.seed) % 256
			var g: int = (x + py * 3 + e.frame_id * 5) % 256
			var b: int = (py * 29 + e.seed * 3) % 256
			if absi(c.r8 - r) > tolerance or absi(c.g8 - g) > tolerance or absi(c.b8 - b) > tolerance or absi(c.a8 - 255) > tolerance:
				bad += 1
	return bad


func _verify(img: Image, e: Dictionary) -> String:
	if e.kind == "null_ok":
		return ""
	if e.kind == "null_expected":
		return "" if img == null else "an image, expected null"
	if img == null:
		return "null image"
	if img.get_size() != Vector2i(e.size):
		return "size %s, expected %s" % [img.get_size(), e.size]
	if e.kind == "pattern":
		if img.get_format() != Image.FORMAT_RGBA8:
			img = img.duplicate()
			img.convert(Image.FORMAT_RGBA8)
		if y_flip == -1:
			y_flip = 1 if _check_pattern(img, e, false) > _check_pattern(img, e, true) else 0
		var bad := _check_pattern(img, e, y_flip == 1)
		if bad > 0:
			var other := _check_pattern(img, e, y_flip == 0)
			var c0 := img.get_pixel(0, 0)
			return "%d/%d pixels wrong (frame_id %d; %d with the other y orientation); (0,0)=%d,%d,%d,%d" % [bad, img.get_width() * img.get_height(), e.frame_id, other, c0.r8, c0.g8, c0.b8, c0.a8]
		return ""
	if e.kind == "pattern_hdr":
		# Half floats (RGBH on both native Vulkan and WebGPU). Measured: the values are the 8-bit pattern's own,
		# not linearized, natively and on the web alike, so compare them as they are, within one 8-bit step.
		var info := "format %d" % img.get_format()
		var raw := img.duplicate()
		raw.convert(Image.FORMAT_RGBA8)
		var bad := _check_pattern(raw, e, y_flip == 1, 1)
		if bad > 0:
			return "%s: %d/%d pixels off by more than 1 (frame_id %d)" % [info, bad, img.get_width() * img.get_height(), e.frame_id]
		return ""
	if e.kind == "fill":
		var want: Array = e.color
		for y in img.get_height():
			for x in img.get_width():
				var c := img.get_pixel(x, y)
				if c.r8 != want[0] or c.g8 != want[1] or c.b8 != want[2] or c.a8 != want[3]:
					return "pixel (%d,%d) = %d,%d,%d,%d, expected %s" % [x, y, c.r8, c.g8, c.b8, c.a8, want]
		return ""
	if e.kind == "bytes":
		if img.get_format() != e.format:
			return "format %d, expected %d" % [img.get_format(), e.format]
		if img.get_data() != e.bytes:
			return "bytes differ (%d vs %d bytes)" % [img.get_data().size(), e.bytes.size()]
		return ""
	if e.kind == "null_ok":
		return ""
	return "unknown expectation"


func _on_image(img: Image, id: int) -> void:
	if mode.begins_with("perf"):
		perf_count += 1
		return
	callbacks[id] += 1
	order_log.append(id)
	var p: Dictionary = pending[id]
	var err := _verify(img, p.expect)
	_record(p.test, err, { "latency_frames": frame - p.frame, "null": img == null, "calls": callbacks[id] })


func _record(p_test: String, p_err: String, p_info := {}) -> void:
	var r := { "ok": p_err == "", "error": p_err }
	r.merge(p_info)
	if results.has(p_test):
		# A second result for one test is a failure of its own (e.g. a callback that fired twice).
		results[p_test + "#dup" + str(results.size())] = { "ok": false, "error": "second result: " + JSON.stringify(r) }
	else:
		results[p_test] = r


func _process(_delta: float) -> void:
	frame += 1
	if mode == "perf_readback":
		get_viewport().get_texture().get_image_async(_on_image.bind(-1))
	if mode.begins_with("perf"):
		for n in WIDTHS:
			_set_frame_id("w%d" % n, frame)
		if frame % 300 == 0:
			var secs := (Time.get_ticks_msec() - perf_t0) / 1000.0
			print("W66PERF frames=%d readbacks=%d secs=%.1f fps=%.1f" % [frame, perf_count, secs, frame / secs])
		return
	if done:
		return
	match frame:
		5, 6, 7:
			# What the synchronous get_image() returns on its first calls (information, not a pass/fail check).
			var img: Image = vps["w96"].vp.get_texture().get_image()
			var kind := "null"
			if img != null:
				kind = "empty" if img.is_empty() else ("zeros" if img.get_data().count(0) == img.get_data().size() else "data")
			_record("info_sync_get_image_frame%d" % frame, "", { "returned": kind })
			# A second texture read synchronously: the web's debug warning is once per texture, so it is counted twice.
			vps["w333"].vp.get_texture().get_image()
		8:
			_test_sync_invalid()
		10:
			# A later frame, with a new pattern this frame: the readback must see this frame's draw.
			for w in WIDTHS:
				_set_frame_id("w%d" % w, 10)
				_request("later_w%d" % w, "w%d" % w)
			_set_frame_id("big", 10)
			_request("later_big", "big")
			_set_frame_id("hdr", 10)
			_request_hdr("later_hdr2d_w333")
		20:
			# Several outstanding requests, same and different textures, then more on the next two frames.
			_set_frame_id("w96", 20)
			_set_frame_id("w333", 20)
			_request("order_a1", "w96")
			_request("order_b1", "w333")
			_request("order_a2", "w96")
			_request("order_b2", "w333")
			_request("order_c1", "w3")
		21:
			_set_frame_id("w96", 21)
			_request("order_a3", "w96")
		22:
			_set_frame_id("w333", 22)
			_request("order_b3", "w333")
			_set_frame_id("w96", 22)
			_request("order_a4", "w96")
		30:
			_test_rd_formats()
		40:
			_test_free_same_frame()
		50:
			_test_free_next_frame_start()
		51:
			_free_marked()
		60:
			_test_rd_texture_free()
		61:
			_free_rd_marked()
		70:
			_test_image_texture()
		80:
			_test_await()
		90:
			_test_awaitable()
		200:
			_finish()


# --- RenderingDevice textures: RGBA8 and RGBA16F at odd widths, through Texture2DRD and RD directly ---

var rd_textures := []


func _half_bytes(w: int, h: int) -> PackedByteArray:
	var b := PackedByteArray()
	b.resize(w * h * 8)
	for y in h:
		for x in w:
			for c in 4:
				# Exactly representable halves, distinct per texel and channel, some above 1.0 (HDR).
				b.encode_half(((y * w + x) * 4 + c) * 2, float((x * 5 + y * 3 + c) % 64) * 0.125 + (2.0 if c == 0 else 0.0))
	return b


func _rgba8_bytes(w: int, h: int) -> PackedByteArray:
	var b := PackedByteArray()
	b.resize(w * h * 4)
	for i in b.size():
		b[i] = (i * 37 + 11) % 256
	return b


func _test_rd_formats() -> void:
	var rd := RenderingServer.get_rendering_device()
	if rd == null:
		_record("rd_formats", "no RenderingDevice")
		return
	for fmt in [RenderingDevice.DATA_FORMAT_R8G8B8A8_UNORM, RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT]:
		var fname := "rgba8" if fmt == RenderingDevice.DATA_FORMAT_R8G8B8A8_UNORM else "rgba16f"
		for w in WIDTHS:
			var h := 5
			var tf := RDTextureFormat.new()
			tf.width = w
			tf.height = h
			tf.format = fmt
			tf.usage_bits = RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT | RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT
			var data := _rgba8_bytes(w, h) if fmt == RenderingDevice.DATA_FORMAT_R8G8B8A8_UNORM else _half_bytes(w, h)
			var rid := rd.texture_create(tf, RDTextureView.new(), [data])
			rd_textures.append(rid)
			var image_format := Image.FORMAT_RGBA8 if fmt == RenderingDevice.DATA_FORMAT_R8G8B8A8_UNORM else Image.FORMAT_RGBAH
			# Through Texture2DRD.get_image_async().
			var t2 := Texture2DRD.new()
			t2.texture_rd_rid = rid
			rd_textures.append(t2) # Keep it alive.
			var id := _new_id("rd_%s_w%d" % [fname, w], { "kind": "bytes", "format": image_format, "bytes": data, "size": Vector2i(w, h) })
			t2.get_image_async(_on_image.bind(id))
			# Straight from RenderingDevice.
			var id2 := _new_id("rd_direct_%s_w%d" % [fname, w], {})
			rd.texture_get_data_async(rid, 0, _on_rd_bytes.bind(id2, data))
	# buffer_get_data_async: a buffer written twice in one frame reads back the second write, and an odd size.
	var first := PackedByteArray()
	first.resize(333)
	for i in first.size():
		first[i] = (i * 13) % 256
	var buf := rd.storage_buffer_create(first.size() + 3, first + PackedByteArray([1, 2, 3]))
	rd_textures.append(buf)
	var id3 := _new_id("rd_buffer_first", {})
	rd.buffer_get_data_async(buf, _on_rd_bytes.bind(id3, first), 0, first.size())
	var second := PackedByteArray()
	second.resize(332)
	for i in second.size():
		second[i] = (i * 7 + 1) % 256
	rd.buffer_update(buf, 0, second.size(), second)
	var id4 := _new_id("rd_buffer_second", {})
	rd.buffer_get_data_async(buf, _on_rd_bytes.bind(id4, second), 0, second.size())


func _on_rd_bytes(bytes: PackedByteArray, id: int, want: PackedByteArray) -> void:
	callbacks[id] += 1
	order_log.append(id)
	var p: Dictionary = pending[id]
	var err := ""
	if bytes != want:
		var first_diff := -1
		for i in min(bytes.size(), want.size()):
			if bytes[i] != want[i]:
				first_diff = i
				break
		err = "bytes differ: got %d bytes, want %d, first difference at %d" % [bytes.size(), want.size(), first_diff]
	_record(p.test, err, { "latency_frames": frame - p.frame, "calls": callbacks[id] })


# --- Freeing while a request is pending ---

var to_free := []


func _test_free_same_frame() -> void:
	var vp := SubViewport.new()
	vp.size = Vector2i(17, 3)
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(vp)
	var id := _new_id("free_same_frame", { "kind": "null_ok" })
	vp.get_texture().get_image_async(_on_image.bind(id))
	vp.queue_free()


func _test_free_next_frame_start() -> void:
	var vp := SubViewport.new()
	vp.size = Vector2i(17, 3)
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(vp)
	var id := _new_id("free_next_frame", { "kind": "null_ok" })
	vp.get_texture().get_image_async(_on_image.bind(id))
	to_free.append(vp)


func _free_marked() -> void:
	for n in to_free:
		n.queue_free()
	to_free.clear()


var rd_to_free := []


func _test_rd_texture_free() -> void:
	var rd := RenderingServer.get_rendering_device()
	var tf := RDTextureFormat.new()
	tf.width = 9
	tf.height = 4
	tf.format = RenderingDevice.DATA_FORMAT_R8G8B8A8_UNORM
	tf.usage_bits = RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT
	var rid := rd.texture_create(tf, RDTextureView.new(), [_rgba8_bytes(9, 4)])
	var t2 := Texture2DRD.new()
	t2.texture_rd_rid = rid
	var id := _new_id("free_rd_texture", { "kind": "null_ok" })
	t2.get_image_async(_on_image.bind(id))
	rd_to_free.append([t2, rid])


func _free_rd_marked() -> void:
	var rd := RenderingServer.get_rendering_device()
	for pair in rd_to_free:
		pair[0].texture_rd_rid = RID()
		rd.free_rid(pair[1])
	rd_to_free.clear()


func _test_image_texture() -> void:
	var img := Image.create_empty(5, 3, false, Image.FORMAT_RGBA8)
	for y in 3:
		for x in 5:
			img.set_pixel(x, y, Color8(x * 40, y * 80, 7, 255))
	var tex := ImageTexture.create_from_image(img)
	rd_textures.append(tex)
	var id := _new_id("image_texture", { "kind": "bytes", "format": Image.FORMAT_RGBA8, "bytes": img.get_data(), "size": Vector2i(5, 3) })
	tex.get_image_async(_on_image.bind(id))


signal image_read(image: Image)


func _test_await() -> void:
	# The pattern Texture2D.get_image_async()'s documentation gives for await.
	_set_frame_id("w96", 80)
	var e := _expected_pattern("w96")
	var f := frame
	vps["w96"].vp.get_texture().get_image_async(image_read.emit)
	var img: Image = await image_read
	_record("await_signal", _verify(img, e), { "latency_frames": frame - f })


# --- Synchronous reads that must not print the web's "not back yet" warning: they fail for a real reason ---

func _test_sync_invalid() -> void:
	# An invalid RID (an error of its own, and no warning).
	RenderingServer.texture_2d_get(RID())
	# An RD texture without TEXTURE_USAGE_CAN_COPY_FROM_BIT (RenderingDevice's own error, and no warning).
	var rd := RenderingServer.get_rendering_device()
	if rd == null:
		return
	var tf := RDTextureFormat.new()
	tf.width = 4
	tf.height = 4
	tf.format = RenderingDevice.DATA_FORMAT_R8G8B8A8_UNORM
	tf.usage_bits = RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT
	var rid := rd.texture_create(tf, RDTextureView.new(), [_rgba8_bytes(4, 4)])
	var t2 := Texture2DRD.new()
	t2.texture_rd_rid = rid
	t2.get_image()
	rd_textures.append(t2)
	rd_textures.append(rid)


# --- The awaitable forms (task 71) ---

# A Texture2D implemented in script: get_image_awaitable() goes through the base class's get_image_async().
class ScriptTexture extends Texture2D:
	var image: Image

	func _get_image() -> Image:
		return image

	func _get_width() -> int:
		return image.get_width()

	func _get_height() -> int:
		return image.get_height()


var emissions := {} # test name -> how many times its signal was emitted


func _await_one(p_test: String, p_signal: Signal, p_expect: Dictionary, p_frame: int) -> void:
	emissions[p_test] = 0
	p_signal.connect(func(_image: Image): emissions[p_test] += 1)
	var img: Image = await p_signal
	_record(p_test, _verify(img, p_expect), { "latency_frames": frame - p_frame, "null": img == null })


func _test_awaitable() -> void:
	for n in ["w96", "w333", "big"]:
		_set_frame_id(n, 90)
	var f := frame
	# Several reads in flight at once, from several functions, two of them of the same texture: each has its own signal.
	_await_one("awaitable_w96_a", vps["w96"].vp.get_texture().get_image_awaitable(), _expected_pattern("w96"), f)
	_await_one("awaitable_w96_b", vps["w96"].vp.get_texture().get_image_awaitable(), _expected_pattern("w96"), f)
	_await_one("awaitable_rs_w333", RenderingServer.texture_2d_get_awaitable(RenderingServer.viewport_get_texture(vps["w333"].vp.get_viewport_rid())), _expected_pattern("w333"), f)
	_await_one("awaitable_rs_invalid_rid", RenderingServer.texture_2d_get_awaitable(RID()), { "kind": "null_expected" }, f)
	var gone := SubViewport.new()
	gone.size = Vector2i(17, 3)
	gone.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(gone)
	_await_one("awaitable_freed_same_frame", gone.get_texture().get_image_awaitable(), { "kind": "null_ok" }, f)
	gone.queue_free()
	var img := Image.create_empty(6, 2, false, Image.FORMAT_RGBA8)
	img.fill(Color8(10, 200, 30, 255))
	var st := ScriptTexture.new()
	st.image = img
	_await_one("awaitable_script_texture", st.get_image_awaitable(), { "kind": "bytes", "format": Image.FORMAT_RGBA8, "bytes": img.get_data(), "size": Vector2i(6, 2) }, f)
	# The documented one-liner, typed.
	var image: Image = await vps["big"].vp.get_texture().get_image_awaitable()
	_record("awaitable_inline", _verify(image, _expected_pattern("big")), { "latency_frames": frame - f })


func _finish() -> void:
	done = true
	for id in pending:
		var p: Dictionary = pending[id]
		if callbacks[id] == 0:
			_record(p.test, "callback never fired")
		elif callbacks[id] > 1:
			results[p.test].ok = false
			results[p.test].error = "callback fired %d times" % callbacks[id]
	for t in emissions:
		if emissions[t] != 1:
			_record(t + "_emitted_once", "signal emitted %d times" % emissions[t])
	# The order_* requests (texture readbacks, three frames) must call back in the order they were made.
	var order_ids := order_log.filter(func(i): return pending[i].test.begins_with("order_"))
	var sorted := order_ids.duplicate()
	sorted.sort()
	_record("callback_order", "" if sorted == order_ids and order_ids.size() == 8 else "order %s" % [order_ids.map(func(i): return pending[i].test)])
	var passed := 0
	for k in results:
		if results[k].ok:
			passed += 1
	print("W66RESULT " + JSON.stringify({ "platform": OS.get_name(), "y_flip": y_flip, "passed": passed, "total": results.size(), "results": results }))
	print("W66DONE")
	if not OS.has_feature("web"):
		get_tree().quit(0 if passed == results.size() else 1)
