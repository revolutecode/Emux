[Main]

local struct Secret {
    a : u8,
    b : u8
}

global Start() i32 {
    local s : Secret = { 3, 4 }
    return s.a + s.b
}