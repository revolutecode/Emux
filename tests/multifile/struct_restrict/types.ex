[Types]

[[packed]]
global struct Pair {
    a : u16,
    b : u16
}

[[packed]]
[[unrestrict]]
global struct Twin {
    a : u16,
    b : u16
}

// [[restrict]] propaga-se cross-module: exige a struct nominal exata.
global SumStrict([[restrict]] Pair p) i32 {
    return p.a + p.b
}

// Sem [[restrict]], aceita uma struct estruturalmente equivalente ([[unrestrict]]).
global SumLoose(Pair p) i32 {
    return p.a + p.b
}