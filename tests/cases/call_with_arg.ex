[Main]
global x : u32

global AddOne(i32 a) i32 {
    return a + 1
}

global Start() i32 {
    x = AddOne(41)
    return x
}
