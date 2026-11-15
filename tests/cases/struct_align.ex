[Main]

[[align(16)]]
struct Aligned {
    a : u8,
    b : u8
}

global g : Aligned

global Start() i32 {
    g.a = 2
    g.b = 3
    return g.a + g.b
}