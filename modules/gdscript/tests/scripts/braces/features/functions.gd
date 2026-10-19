func add(a: int, b: int = 10) -> int:
	return a + b


func nothing() -> void:
	pass


func typed_dict() -> Dictionary[String, int]:
	return {"one": 1}


func sum_all(...values: Array) -> int:
	var total := 0
	for v in values: total += v
	return total


static func shout(text: String) -> String: return text.to_upper()


func early(flag: bool) -> String:
	if flag: return "early"
	return "late"


func annotated():
	@warning_ignore("unused_variable")
	var unused = 1


func returns_dictionary() -> Dictionary:
	return {
		"nested": {"deep": [1, {"deeper": true}]},
	}


func test():
	print(add(1))
	print(add(1, 2))
	nothing()
	print(typed_dict())
	print(sum_all(1, 2, 3))
	print(shout("hi"))
	print(early(true), " ", early(false))
	annotated()
	print(returns_dictionary())
