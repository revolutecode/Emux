[Main]
var a : f32
var i : i32

func Start() i32 {
    a = 10000000000000000000000000000000000000000
    i = static_cast<i32>(a)
    return i + 42
}
