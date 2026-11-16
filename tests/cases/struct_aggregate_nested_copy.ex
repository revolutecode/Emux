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

global Start() i32 {
    local q : Pair = { 8, 9 }
    local w : Wrap = { q, 1 }
    return w.inner.a + w.inner.b + w.tag
}