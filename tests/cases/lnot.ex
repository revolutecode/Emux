[Main]
global x : u32

global Start() u8 {
    x = 0
    x = !x
    return x
}
