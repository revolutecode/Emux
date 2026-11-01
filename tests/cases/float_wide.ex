[Main]
var a : f128
var b : f128
var i : i32

func Start() i32 {
    a = 10
    b = 4
    i = static_cast<i32>(a / b)
    a = a + b
    b = a * 3
    return i + static_cast<i32>(b) - 2
}
