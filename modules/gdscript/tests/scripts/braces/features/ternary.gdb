func pick(x: int) -> String {
	return x > 10 ? "big" : x > 5 ? "medium" : "small"
}

func test() {
	print(pick(20), " ", pick(7), " ", pick(1))
	var a := 3
	var b := (a > 2 ? a : 0) + 1
	print(b)
	var d = { "key": a == 3 ? "three" : "other", "nested": (a > 1 ? (a > 2 ? "both" : "one") : "none") }
	print(d)
	var arr = [true ? 1 : 2, false ? { "x": 1 } : { "y": 2 }]
	print(arr)
	var f = func(v) { return v ? "yes" : "no" }
	print(f.call(true), " ", f.call(false))
	print("mixed: ", "a" if a == 3 else "b", " ", a == 3 ? "c" : "d")
}
