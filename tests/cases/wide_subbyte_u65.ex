[Main]
global a : u65

global Start() i32 {
    a = 36893488147419103231
    a = a + 1
    return static_cast<i32>(a >> 64)
}
