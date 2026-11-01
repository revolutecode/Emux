[Main]
var a : u24

func Start() i32 {
    a = 16777215
    a = a + 1
    return static_cast<i32>(a >> 23)
}
