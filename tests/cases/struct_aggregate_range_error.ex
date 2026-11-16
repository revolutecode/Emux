[Main]

[[packed]]
struct Pair {
    a : u16,
    b : u16
}

global Start() i32 {
    local p : Pair = { 70000, 2 }
    return p.a
}