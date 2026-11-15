[Main]
global x : u32

global AddOne(i32 a) i32 {
    return a + 1
}

global Start() i32 {
    x = static_cast<u32>(AddOne(41))
    return static_cast<i32>(x)
}
