[Main]

[[packed]]
struct Pair {
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

global Start() i32 {
    local p : Pair = { 10, 20 }
    local q : Pair = Make()
    return Sum(p) + Sum(q)
}