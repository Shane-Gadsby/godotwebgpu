func apply(f: Callable, value: int) -> int {
	return f.call(value)
}

func test() {
	var double = (x) => x * 2
	print(double.call(4))
	var add = (a: int, b: int = 10) => a + b
	print(add.call(1), " ", add.call(1, 2))
	var square = x => x * x
	print(square.call(5))
	print([1, 2, 3].map(x => x * 3))
	print([1, 2, 3, 4].filter((x) => x % 2 == 0))
	print(apply((x) => x - 1, 5))
	var block = (x) => {
		var y = x + 1
		return y * 10
	}
	print(block.call(1))
	var table: Dictionary[String, Callable] = { "inc": (x) => x + 1, "dec": x => {
		return x - 1
	} }
	print(table["inc"].call(1), " ", table["dec"].call(1))
	var nested = (x) => (y) => x + y
	@warning_ignore("unsafe_method_access")
	print(nested.call(2).call(3))
	var shout = () => print("void call from an arrow")
	print(shout.call())
	var pick = (n) => n > 0 ? "positive" : "not positive"
	print(pick.call(1), " ", pick.call(-1))
	var no_args = () => 42
	print(no_args.call())
}
