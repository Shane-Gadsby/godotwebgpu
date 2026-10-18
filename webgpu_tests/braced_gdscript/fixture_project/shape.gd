class_name Shape
extends RefCounted

## An indented base class that braced scripts extend.

var name := "shape"


func area() -> float:
	return 0.0


func describe() -> String:
	return "%s area=%.1f" % [name, area()]
