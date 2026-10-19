# Dispatcher for the congestion tier. Picks the fixture AND the role, since
# every fixture here runs as two processes.
#
#   native:  godot --headless --path . -- <test> <role> [signal_url]
#   web:     index.html?test=<test>&role=<role>&signal=<url>
#
# role is "offerer" or "answerer". Defaults: test=backpressure, role=offerer,
# signal=ws://127.0.0.1:9099.
extends Node

const DEFAULT_SIGNAL := "ws://127.0.0.1:9099"

func _bail(msg: String, code: int) -> void:
	# Print in the same shape cong_base.out() uses, so a dispatcher failure is
	# visible to the runner's log scraping rather than looking like a hang --
	# the exact trap the parity tier hit twice (webrtc_web.md §7 correction 4).
	var line := "RTC| " + msg
	print(line)
	print("RTC| END")
	if OS.has_feature("web"):
		JavaScriptBridge.eval(
			"(window.__rtc_lines=window.__rtc_lines||[]).push(%s);" % JSON.stringify(line), true)
		JavaScriptBridge.eval("window.__rtc_done = true;", true)
	else:
		get_tree().quit(code)

func _ready() -> void:
	var which := "backpressure"
	var role := "offerer"
	var url := DEFAULT_SIGNAL

	if OS.has_feature("web"):
		var q = JavaScriptBridge.eval("""
			(function () {
				var p = new URLSearchParams(window.location.search);
				return JSON.stringify([p.get('test') || '', p.get('role') || '', p.get('signal') || '']);
			})()
		""", true)
		if typeof(q) == TYPE_STRING:
			var parsed = JSON.parse_string(q)
			if parsed is Array and parsed.size() == 3:
				if str(parsed[0]) != "":
					which = str(parsed[0])
				if str(parsed[1]) != "":
					role = str(parsed[1])
				if str(parsed[2]) != "":
					url = str(parsed[2])
	else:
		var args := OS.get_cmdline_user_args()
		if args.size() > 0 and args[0] != "":
			which = args[0]
		if args.size() > 1 and args[1] != "":
			role = args[1]
		if args.size() > 2 and args[2] != "":
			url = args[2]

	if role != "offerer" and role != "answerer":
		_bail("FATAL unknown role '%s' (offerer|answerer)" % role, 1)
		return

	var path := "res://tests/%s.gd" % which
	if not ResourceLoader.exists(path):
		_bail("FATAL unknown test '%s'" % which, 1)
		return
	var script: Variant = load(path)
	var node: Node = script.new() if script is GDScript else null
	if node == null:
		_bail("FATAL '%s' failed to load (see the parse error above)" % which, 1)
		return
	node.name = "Fixture"
	node.setup(role, which, url)
	add_child(node)
