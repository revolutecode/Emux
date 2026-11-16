[Main]

struct Rec {
    a : u8,
    b : u32
}

global Start() i32 {
    local r : Rec = { 1, 512 }
    r.a = 2
    return r.a + r.b / 256
}