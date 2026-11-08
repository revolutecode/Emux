[Main]

var i : i32
var sum : i32

func Start() i32 {
    sum = 0
    for (i = 0; i < 10; i = i + 1) {
        if (i % 2 == 1) {
            continue
        }
        sum = sum + i
    }
    return sum
}
