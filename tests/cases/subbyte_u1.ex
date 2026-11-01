[Main]
var a : u1

func Start() i32 {
    a = 1
    a = a + 1
    return static_cast<i32>(a) + 42
}
