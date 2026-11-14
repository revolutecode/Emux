[Main]
global x : u32
global y : u32

global Start() u8 {
    x = 0xFF
    y = 1 << 3
    return y
}
