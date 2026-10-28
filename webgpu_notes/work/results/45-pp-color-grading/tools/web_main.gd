extends Node3D
# Web copy of proj/grade_test.gd: the chart under one grading case, from the URL query (case=, chart=).
# Sets document.title to "ready" after 12 frames.

const Cases = preload("res://cases.gd")
var frames := 0

func _ready() -> void:
	var args := {}
	var query: String = str(JavaScriptBridge.eval("location.search")).trim_prefix("?")
	for a in query.split("&", false):
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			args[kv[0]] = kv[1].uri_decode()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED
	Cases.apply(env, args.get("case", "off"))
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var cam := Camera3D.new()
	cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	cam.size = 1.0
	cam.position = Vector3(0, 0, 1)
	add_child(cam)
	add_child(preload("res://chart.gd").make_chart(args.get("chart", "lattice")))

func _process(_delta: float) -> void:
	frames += 1
	if frames == 12:
		JavaScriptBridge.eval("document.title = 'ready'")
