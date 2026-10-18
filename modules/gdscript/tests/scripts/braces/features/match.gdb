func describe(value: Variant) -> String {
	match value {
		0 { return "zero" }
		1, 2 { return "one or two" }
		[1, 2] { return "array 1, 2" }
		[var first, ..] { return "array starting with %s" % first }
		{"kind": "dict"} { return "dict pattern" }
		{"key": var v} { return "dict with key %s" % v }
		var n when n is int and n > 100 { return "big %s" % n }
		_ { return "other" }
	}
}

func test() {
	for v in [0, 1, 2, [1, 2], [7, 8, 9], {"kind": "dict"}, {"key": 5}, 500, "text"] {
		print(describe(v))
	}

	match 3 {
		3 {
			print("multi-line branch")
			print("second statement")
		}
		_ {}
	}

	match {"kind": "dict"} {
		{"kind": "dict"} { print("dictionary as the match value and first pattern") }
	}
}
