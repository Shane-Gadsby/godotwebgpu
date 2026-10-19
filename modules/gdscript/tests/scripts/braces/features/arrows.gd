func apply(f: Callable, value: int) -> int:
	return f.call(value)


func test():
	var double = func(x): return x * 2
	print(double.call(4))
	var add = func(a: int, b: int = 10): return a + b
	print(add.call(1), " ", add.call(1, 2))
	var square = func(x): return x * x
	print(square.call(5))
	print([1, 2, 3].map(func(x): return x * 3))
	print([1, 2, 3, 4].filter(func(x): return x % 2 == 0))
	print(apply(func(x): return x - 1, 5))
	var block = func(x):
		var y = x + 1
		return y * 10
	print(block.call(1))
	var table: Dictionary[String, Callable] = { "inc": func(x): return x + 1, "dec": func(x):
		return x - 1
	}
	print(table["inc"].call(1), " ", table["dec"].call(1))
	var nested = func(x): return func(y): return x + y
	@warning_ignore("unsafe_method_access")
	print(nested.call(2).call(3))
	var shout = func(): print("void call from an arrow")
	print(shout.call())
	var pick = func(n): return "positive" if n > 0 else "not positive"
	print(pick.call(1), " ", pick.call(-1))
	var no_args = func(): return 42
	print(no_args.call())
