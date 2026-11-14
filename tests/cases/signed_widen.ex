[Main]
global a : u8
global b : u32
global c : i8

global Start() u8 {
    a = 200
    b = a
    c = -5
    b = c
    return b & 0xFF
}
