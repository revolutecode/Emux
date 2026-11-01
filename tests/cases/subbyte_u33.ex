[Main]
var a : u33

func Start() i32 {
    a = 8589934591
    a = a + 1
    return static_cast<i32>(a >> 32)
}
