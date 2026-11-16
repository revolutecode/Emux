[Types]

[[packed]]
global struct Pair {
    a : u16,
    b : u16
}

global Sum(Pair p) i32 {
    return p.a + p.b
}

global Make() Pair {
    local r : Pair = { 3, 4 }
    return r
}