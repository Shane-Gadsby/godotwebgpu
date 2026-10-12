# Dispatcher: picks which fixture to run. Native takes it from user args
# (`-- mesh`), web from a `?test=` query parameter. Defaults to loopback.
extends Node

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
	print("RTC| env web=%s threads=%s" % [
		str(OS.has_feature("web")), str(OS.has_feature("threads"))])
	print("RTC| test=%s" % which)
	var path := "res://tests/%s.gd" % which
	if not ResourceLoader.exists(path):
		print("RTC| FATAL unknown test '%s'" % which)
		print("RTC| END")
		if OS.has_feature("web"):
			JavaScriptBridge.eval("window.__rtc_done = true;", true)
		else:
			get_tree().quit(1)
		return
	var node = load(path).new()
	node.name = "Fixture"
	add_child(node)
