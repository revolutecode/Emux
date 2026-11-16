[Main]

struct Rec {
    a : u32,
    b : u32
}

global GetB(Rec r) i32 {
    return r.b / 256
}

global Make() Rec {
    local r : Rec = { 1, 512 }
    return r
}

global Start() i32 {
    local r : Rec = Make()
    return r.a + GetB(r)
}