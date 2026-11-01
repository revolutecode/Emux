[Main]
var a : q32.16
var b : q32.16
var i : i32

func Start() i32 {
    a = 5
    b = 0
    a = a / b
    i = static_cast<i32>(a)
    return i + 42
}
