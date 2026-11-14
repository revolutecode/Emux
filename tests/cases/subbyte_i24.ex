[Main]
global a : i24

global Start() i32 {
    a = 8388607
    a = a + a
    a = a >> 23
    return static_cast<i32>(a & 255)
}
