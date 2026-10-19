class Base extends RefCounted {
	var name := "base"

	func greet() -> String {
		return "hello from %s" % name
	}
}

class Child extends Base {
	func _init() {
		name = "child"
	}

	func greet() -> String {
		return super() + "!"
	}
}

enum { ANON_A, ANON_B }
enum Named { ONE = 1, TWO, THREE }
enum Multi {
	X,
	Y,
}

var _backing := 0
var with_accessors: int {
	get {
		return _backing * 2
	}
	set(value) {
		_backing = value
	}
}

var short_form: int = 5 {
	get { return short_form + 1 }
	set(value) { short_form = value }
}

var via_methods: int { get = _get_via, set = _set_via }
func _get_via() -> int { return 42 }
func _set_via(_value: int) -> void {}

static var counter := 0
static func bump() -> int {
	counter += 1
	return counter
}

func test() {
	print(Base.new().greet())
	print(Child.new().greet())
	print(ANON_B, " ", Named.THREE, " ", Multi.Y)
	with_accessors = 21
	print(with_accessors)
	print(short_form)
	print(via_methods)
	print(bump())
	print(bump())
}
