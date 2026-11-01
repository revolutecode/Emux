[Main]
var a : f16

func Start() i32 {
    a = 10
    a = a / 4
    a = a * 8
    return static_cast<i32>(a) + 22
}
