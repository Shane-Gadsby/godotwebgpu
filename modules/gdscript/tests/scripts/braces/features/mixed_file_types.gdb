const Child = preload("gd_extends_braced_class.notest.gd")


func test() {
	var base: BracedGlobalBase = BracedGlobalBase.new()
	print(base.bump())
	var child: BracedGlobalBase = Child.new()
	print(child.bump())
	print(child is BracedGlobalBase)
}
