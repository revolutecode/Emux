[Main]
var a : u8
var b : u32
var c : i8

func Start() u8 {
    a = 200
    b = a
    c = -5
    b = c
    return b & 0xFF
}
