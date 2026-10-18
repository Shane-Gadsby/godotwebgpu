class_name BracedGlobalBase extends RefCounted

var counter := 0

func bump() -> int {
	counter += 1
	return counter
}
