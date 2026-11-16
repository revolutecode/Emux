[Main]

struct Big {
    a : u32,
    b : u32,
    c : u32
}

global Use(Big b) i32 {
    return b.a
}

global Start() i32 {
    local x : Big
    return Use(x)
}