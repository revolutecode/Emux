[Main]

[[packed]]
struct Pair {
    a : u16,
    b : u16
}

global g : Pair = { 1, 2 }

global Start() i32 {
    return g.a
}