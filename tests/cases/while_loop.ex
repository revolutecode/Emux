[Main]

var i : i32
var sum : i32

func Start() i32 {
    i = 1
    sum = 0
    while (i <= 5) {
        sum = sum + i
        i = i + 1
    }
    return sum
}
