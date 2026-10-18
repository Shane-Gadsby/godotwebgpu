extends Node

## A braced script for the language server.
var count: int = 0

func add(amount: int) -> int {
count += amount
        return count
}
