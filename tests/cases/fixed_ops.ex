[Main]
var a : q32.16
var b : q32.16
var i : i32

func Start() i32 {
    a = 7
    b = 2
    i = static_cast<i32>(a / b)
    a = a * b
    b = 4
    b = a % b
    a = a - 4
    b = -b
    i = i + static_cast<i32>(a)
    i = i + static_cast<i32>(b)
    return i + static_cast<i32>(a + b) + 23
}
