[Main]
var a : f32
var b : f32
var i : i32

func Start() i32 {
    a = 10
    b = 4
    i = static_cast<i32>(a / b)
    a = a + b
    b = a * 2
    a = b - 20
    b = -b
    i = i + static_cast<i32>(a)
    i = i + static_cast<i32>(b)
    return i + 60
}
