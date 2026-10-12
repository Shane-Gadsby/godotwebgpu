# Dispatcher: picks which fixture to run. Native takes it from user args
# (`-- mesh`), web from a `?test=` query parameter. Defaults to loopback.
#
# Extends the fixture base purely to share out()/finish(), so the dispatcher's
# own lines reach window.__rtc_lines the same way a fixture's do -- otherwise
# the Safari runner, which can only read that array, would see a fixture's
# output but not the "env"/"test" header or a FATAL.
extends "res://tests/rtc_test_base.gd"

func _bail(code: int) -> void:
	out("END")
	if OS.has_feature("web"):
		JavaScriptBridge.eval("window.__rtc_done = true;", true)
	else:
		get_tree().quit(code)

func _ready() -> void:
	var which := "loopback"
	if OS.has_feature("web"):
		var q = JavaScriptBridge.eval("new URLSearchParams(window.location.search).get('test') || ''", true)
		if typeof(q) == TYPE_STRING and q != "":
			which = q
	else:
		var args := OS.get_cmdline_user_args()
		if args.size() > 0 and args[0] != "":
			which = args[0]
	# Self-document the variant: "threads" is the tag web_main.cpp itself checks,
	# so a threaded export is distinguishable from a nothreads one in the log.
	out("env web=%s threads=%s" % [
		str(OS.has_feature("web")), str(OS.has_feature("threads"))])
	out("test=%s" % which)
	var path := "res://tests/%s.gd" % which
	if not ResourceLoader.exists(path):
		out("FATAL unknown test '%s'" % which)
		_bail(1)
		return
	# A script that fails to parse used to leave the SceneTree looping forever
	# with nothing more printed, which is indistinguishable from a hang -- and
	# has now cost two debugging sessions (webgpu_notes/webrtc_web.md §7
	# correction 4). Fail loudly instead; the parse error itself is above this
	# line in the output.
	var script: Variant = load(path)
	var node: Node = script.new() if script is GDScript else null
	if node == null:
		out("FATAL '%s' failed to load (see the parse error above)" % which)
		_bail(1)
		return
	node.name = "Fixture"
	add_child(node)
