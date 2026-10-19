func test():
	var x = 3
	if x > 5:
		print("big")
	elif x > 2:
		print("medium")
	else:
		print("small")


	if x == 3: print("single line if")
	if x == 4: print("no")
	else: print("single line else")
	if x == 1: print("a")
	elif x == 3: print("single line elif")
	else: print("c")

	if x == 0:
		print("no")
	else:
		print("else on its own line")

	var i = 0
	while i < 3:
		i += 1
		if i == 2:
			continue
		print("while ", i)

	for n in range(5):
		if n == 3: break
		print("for ", n)

	for k in {"a": 1, "b": 2}:
		print("key ", k)

	if (x > 1 and
			x < 10):
		print("multi-line condition")

	if x == 3:
		if x > 0:
			if x < 9: print("nested on one line")

	if x == 1:
		print("one")
	elif x == 3:
		print("else if")
	else:
		print("other")
	if x == 2: print("no")
	elif x == 3: print("else if on one line")
	var else_if_value = 1 if x == 0 else 2 if x == 3 else 4
	print(else_if_value)

	var y = 1 if x > 2 else 2
	print(y)
	var z = {"k": 1} if x > 2 else {}
	print(z)
	var d = {}
	if d == {}: print("compared with an empty dictionary")
