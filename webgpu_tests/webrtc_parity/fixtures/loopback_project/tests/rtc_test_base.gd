# Shared base for every WebRTC parity fixture. All output is prefixed "RTC|" so
# the native run and the browser run can be diffed line for line.
#
# Fixtures MUST drive themselves from _process(), never a blocking loop: the web
# implementation is callback-driven from the browser event loop, so a loop that
# does not yield starves every ICE/SDP/onmessage callback and nothing connects.
extends Node

var ticks := 0
var phase := "start"

func out(s: String) -> void:
	print("RTC| " + s)

func finish() -> void:
	phase = "done"
	out("END")
	if OS.has_feature("web"):
		JavaScriptBridge.eval("window.__rtc_done = true;", true)
	else:
		get_tree().quit()
