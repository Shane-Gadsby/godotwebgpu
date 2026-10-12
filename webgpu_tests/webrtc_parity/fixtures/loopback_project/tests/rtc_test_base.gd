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
	var line := "RTC| " + s
	print(line)
	if OS.has_feature("web"):
		# Mirror every line onto window.__rtc_lines as well as the console.
		# Safari is driven by AppleScript (`do JavaScript`), which cannot read
		# the console at all, so the array is the only way to read a fixture's
		# output there -- and it also means a browser that drops or truncates
		# console messages shortens the diff visibly rather than silently.
		# JSON.stringify keeps payloads containing quotes from breaking the eval.
		JavaScriptBridge.eval(
			"(window.__rtc_lines=window.__rtc_lines||[]).push(%s);" % JSON.stringify(line), true)

func finish() -> void:
	phase = "done"
	out("END")
	if OS.has_feature("web"):
		JavaScriptBridge.eval("window.__rtc_done = true;", true)
	else:
		get_tree().quit()
