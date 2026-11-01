[Main]
var a : f24
var b : f24

func Start() i32 {
    a = 10
    b = 4
    a = a / b
    b = a * 8
    return static_cast<i32>(b) + 22
}
