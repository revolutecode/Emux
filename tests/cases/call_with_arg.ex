[Main]
var x : u32

func AddOne(i32 a) i32 {
    return a + 1
}

func Start() i32 {
    x = AddOne(41)
    return x
}
