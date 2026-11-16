[Main]

[[packed]]
struct Pair {
    a : u16,
    b : u16
}

global Mutate(Pair p) i32 {
    p.a = 99
    return p.a
}

global Start() i32 {
    local p : Pair = { 5, 6 }
    local m : i32 = Mutate(p)
    return p.a + m
}