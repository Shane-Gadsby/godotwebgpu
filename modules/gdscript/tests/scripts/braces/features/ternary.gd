func pick(x: int) -> String:
	return "big" if x > 10 else "medium" if x > 5 else "small"


func test():
	print(pick(20), " ", pick(7), " ", pick(1))
	var a := 3
	var b := (a if a > 2 else 0) + 1
	print(b)
	var d = { "key": "three" if a == 3 else "other", "nested": (("both" if a > 2 else "one") if a > 1 else "none") }
	print(d)
	var arr = [1 if true else 2, { "x": 1 } if false else { "y": 2 }]
	print(arr)
	var f = func(v): return "yes" if v else "no"
	print(f.call(true), " ", f.call(false))
	print("mixed: ", "a" if a == 3 else "b", " ", "c" if a == 3 else "d")
