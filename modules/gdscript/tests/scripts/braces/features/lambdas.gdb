func apply(f: Callable, value: int) -> int {
	return f.call(value)
}

func test() {
	var double = func(x) { return x * 2 }
	print(double.call(4))

	var multi = func(x) {
		var y = x + 1
		return y * 10
	}
	print(multi.call(1))

	print(apply(func(x) { return x - 1 }, 5))

	print(apply(func(x) {
		var r = x * x
		return r
	}, 6))

	var arr = [1, 2, 3].map(func(x) { return x * 3 })
	print(arr)

	var filtered = [1, 2, 3, 4].filter(func(x) {
		return x % 2 == 0
	})
	print(filtered)

	var table: Dictionary[String, Callable] = {
		"inc": func(x) { return x + 1 },
		"dec": func(x) {
			return x - 1
		},
	}
	print(table["inc"].call(1), " ", table["dec"].call(1))

	var nested = func(x) {
		var inner = func(y) { return y + x }
		return inner.call(100)
	}
	print(nested.call(5))

	var named = func named_lambda(x) { return "named %s" % x }
	print(named.call(1))

	var list: Array[Callable] = [func() { return "in array" }]
	print(list[0].call())

	var no_body = func() {}
	print(no_body.call())

	var typed = func(a: int, b: int) -> int { return a * b }
	print(typed.call(6, 7))
}
