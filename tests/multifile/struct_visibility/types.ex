[Types]

local struct Secret {
    a : u8
}

global struct Shared {
    x : u8,
    y : u8
}

global UseSecret() i32 {
    local s : Secret = { 5 }
    return s.a
}