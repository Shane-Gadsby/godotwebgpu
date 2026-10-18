extends Node

// An autoload written in braced GDScript.
signal bumped(count: int)

var count := 0

func bump() {
	count += 1
	bumped.emit(count)
}
