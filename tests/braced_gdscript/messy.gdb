extends Node
# A comment
class_name Messy
var speed:int=10
var table={"a":1,"b":[1,2,3]}
func _ready()->void{
print("hi",speed)
      if speed>5 and not is_inside_tree(){
  print( "fast" )
}
else{
print("slow")}
var double=func(x){return x*2}
print(double.call(-speed))
for i in range(3){if i==1{continue}
print(i)}
}
func helper(a:int,b:=2)->int{return a+b}
static func s()->void{}
