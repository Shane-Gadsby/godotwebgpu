class_name Circle
extends Shape

/** A braced class extending an indented one. */
var radius: float = 1.0 {
	get { return radius }
	set(value) { radius = maxf(value, 0.0) }
}

func _init(r: float = 1.0) {
	name = "circle"
	radius = r
}

func area() -> float {
	return PI * radius * radius
}
