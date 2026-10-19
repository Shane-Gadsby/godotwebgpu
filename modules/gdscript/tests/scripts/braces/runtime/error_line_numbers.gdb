func test() {
	var d := {}
	if d.is_empty() { print("a") } else { print("b") }
	if true { print("before") } else { print("never") } if d.is_empty() { print(d.missing) }
	print("not reached")
}
