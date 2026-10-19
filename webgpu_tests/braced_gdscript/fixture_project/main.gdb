extends Node2D

// Self-checking scene for braced GDScript in an export: prints "[GDB-CHECK] PASS", or "[GDB-CHECK] FAIL" with what
// failed. Run with `-- --quit-on-check` to quit afterwards (the native check in tests/braced_gdscript/run.sh).

const Labeled = preload("res://labeled.gd")

var failures: Array[String] = []
var frames := 0

func check(what: String, ok: bool) {
	if !ok {
		failures.append(what)
	}
}

func classify(value: Variant) -> String {
	match value {
		[_, _] { return "pair" }
		{ "kind": var kind } { return "dict " + str(kind) }
		var n when n is int && n > 100 { return "big" }
		_ { return "other" }
	}
}

func grade(score: int) -> String {
	if score >= 90 {
		return "A"
	} else if score >= 80 {
		return "B"
	} else if score >= 70 {
		return "C"
	} else {
		return "F"
	}
}

func _ready() {
	check("class_name of a .gdb class extending a .gd class", is_equal_approx(Circle.new(2.0).area(), PI * 4.0))
	check("property accessors", Circle.new(-3.0).radius == 0.0)
	check(".gd extending .gdb by path", Labeled.new(1.0).describe() == "circle area=3.1 labeled")
	check("arrow lambdas", [1, 2, 3].map((x) => x * 2) == [2, 4, 6])
	check("func lambdas", [1, 2, 3, 4].filter(func(x) { return x % 2 == 0 }) == [2, 4])
	check("ternary", (5 > 3 ? "yes" : "no") == "yes")
	check("match", classify([1, 2]) == "pair" && classify({ "kind": "x" }) == "dict x" && classify(500) == "big")
	check("else if and elif", grade(95) == "A" && grade(85) == "B" && grade(75) == "C" && grade(1) == "F")

	var seen := [0]
	Registry.bumped.connect((count) => { seen[0] = count })
	Registry.bump()
	check("autoload and signal", Registry.count == 1 && seen[0] == 1)

	// Awaiting a frame: the script keeps running across frames.
	await get_tree().process_frame
	await get_tree().process_frame
	check("await", frames >= 1)

	if failures.is_empty() {
		print("[GDB-CHECK] PASS")
	} else {
		print("[GDB-CHECK] FAIL: " + ", ".join(failures))
	}
	if OS.get_cmdline_user_args().has("--quit-on-check") {
		get_tree().quit(0 if failures.is_empty() else 1)
	}
}

func _process(_delta: float) {
	frames += 1
	queue_redraw()
}

func _draw() {
	// Something to see: a disc whose color says the result once it is known.
	var color := Color(0.3, 0.3, 0.3)
	if frames > 2 {
		color = failures.is_empty() ? Color(0.2, 0.8, 0.3) : Color(0.9, 0.2, 0.2)
	}
	draw_circle(Vector2(320, 240), 100.0, color)
}
