
# ─── WebGPU smoketest instrumentation ────────────────────────────────────────
# Appended to the upstream demo's main.gd by run_scenes.mjs before export, and
# removed again afterwards. The demo is interactive -- the compute shader only
# runs when a button is pressed -- so without this the scene loads, does nothing,
# and a broken compute path would still "pass".
#
# _enter_tree() is used because the upstream script does not define it, so this
# appends cleanly without editing any existing function body.

func _enter_tree() -> void:
	_webgpu_heightmap_selftest.call_deferred()


func _webgpu_heightmap_selftest() -> void:
	# On WebGPU, texture readback is asynchronous: the first texture_2d_get() call
	# starts it and returns an empty Image, and the data arrives on a later frame
	# (see TextureStorage::texture_2d_get, which documents exactly this). The demo's
	# init_gpu() reads gradient_tex.get_image().get_data() once and would get
	# nothing, failing texture_create() for the gradient and cascading into a
	# uniform-set error that reads like a driver bug. So prime the readback here
	# until it lands, then let the demo's own single call hit the warmed cache.
	# On Vulkan the first call already returns data and this loop exits immediately.
	var primed := false
	for _i in range(120):
		await get_tree().process_frame
		var img := gradient_tex.get_image()
		if img != null and not img.is_empty():
			primed = true
			break
	if not primed:
		print("[HEIGHTMAP-CHECK] FAIL gradient texture readback never completed")
		return

	var heightmap := prepare_image()
	var input_bytes := heightmap.get_data()

	compute_island_gpu(heightmap)

	if rd == null:
		print("[HEIGHTMAP-CHECK] FAIL no local RenderingDevice")
		return

	var out_bytes := rd.texture_get_data(heightmap_rid, 0)
	if out_bytes.size() != input_bytes.size():
		print("[HEIGHTMAP-CHECK] FAIL readback size %d != %d" % [out_bytes.size(), input_bytes.size()])
		return

	# The shader multiplies the noise by a radial gradient and floors anything
	# below 0.2 to zero, so a correct result has three properties the input does
	# not: it differs from the input, it is not uniformly zero, and the center is
	# brighter than the corners. Checking the gradient's *direction* is the part
	# that catches a plausibly-wrong result -- a readback of the untouched input,
	# or of a blank texture, passes a "did anything change" test but fails this.
	var dim := po2_dimensions
	var centre_sum := 0
	var centre_n := 0
	var corner_sum := 0
	var corner_n := 0
	var nonzero := 0
	var differs := 0
	var step := maxi(1, dim / 64) # Sample a grid rather than every texel.
	for y in range(0, dim, step):
		for x in range(0, dim, step):
			var i := y * dim + x
			var v: int = out_bytes[i]
			if v != 0:
				nonzero += 1
			if v != input_bytes[i]:
				differs += 1
			var dx := float(x - dim / 2) / float(dim / 2)
			var dy := float(y - dim / 2) / float(dim / 2)
			var r := sqrt(dx * dx + dy * dy)
			if r < 0.25:
				centre_sum += v
				centre_n += 1
			elif r > 0.9:
				corner_sum += v
				corner_n += 1

	if nonzero == 0:
		print("[HEIGHTMAP-CHECK] FAIL output is entirely zero")
		return
	if differs == 0:
		print("[HEIGHTMAP-CHECK] FAIL output identical to input (compute did not run)")
		return
	if centre_n == 0 or corner_n == 0:
		print("[HEIGHTMAP-CHECK] FAIL sampling produced no center/corner texels")
		return

	var centre_avg := float(centre_sum) / float(centre_n)
	var corner_avg := float(corner_sum) / float(corner_n)
	if centre_avg <= corner_avg:
		print("[HEIGHTMAP-CHECK] FAIL center %.1f not brighter than corners %.1f" % [centre_avg, corner_avg])
		return

	print("[HEIGHTMAP-CHECK] PASS center %.1f corners %.1f nonzero %d/%d" % [centre_avg, corner_avg, nonzero, centre_n + corner_n])
