[Main]
global a : u1

global Start() i32 {
    a = 1
    a = a + 1
    return static_cast<i32>(a) + 42
}
