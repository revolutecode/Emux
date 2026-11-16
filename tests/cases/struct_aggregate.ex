[Main]

[[packed]]
struct Pair {
    a : u16,
    b : u16
}

[[packed]]
struct Wrap {
    inner : Pair,
    tag : u8
}

global g : Pair

global Start() i32 {
    local p : Pair = { 5, 7 }
    p = { 10, 20 }
    g = { 1, 2 }
    local w : Wrap = { { 3, 4 }, 9 }
    w.inner = { 30, 40 }
    return p.a + p.b + g.a + g.b + w.inner.a + w.inner.b + w.tag
}