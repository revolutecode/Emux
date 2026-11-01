[Main]
var a : q128.16
var b : q128.16
var i : i32

func Start() i32 {
    a = 10
    b = 4
    i = static_cast<i32>(a / b)
    a = a * b
    b = a + b
    return i + static_cast<i32>(b) - 4
}
