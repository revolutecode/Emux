[Main]

[[packed]] struct Pair { a : u16, b : u16 }

// [[restrict]] exige a identidade nominal exata: o argumento e do mesmo tipo.
global Sum([[restrict]] Pair p) i32 {
    return p.a + p.b
}

global Start() i32 {
    local p : Pair = { 10, 20 }
    return Sum(p)
}