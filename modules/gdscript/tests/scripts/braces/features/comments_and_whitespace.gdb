// A line comment.
/* A block comment
   over several lines. */
/// A documentation comment.
func test() {
print("no indentation at all")   // trailing comment
		if true {
  print("odd indentation")
	      print("mixed tabs and spaces")
}
	var total = 1 /* inline block comment */ + 2
	print(total)
	var quotient = 10 / 2   # hash comments still work
	print(quotient)
	/* comment before code */ print("after a block comment")
	var d = {
		"a": 1, // comment inside a dictionary
		"b": 2,
	}
	print(d)
	if total == 3 { /* comment in a block */ print("ok") }
	if total == 4 { /* only a comment */ }
	var spread = [
1,
        2,
  3]
	print(spread)
	print("end")
}
