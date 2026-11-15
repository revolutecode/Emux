[Main]

struct Inner {
    a : u8,
    b : u32
}

struct Outer {
    inner : Inner,
    c : u8
}

global Start() i32 {
    local o : Outer
    o.inner.a = 3
    o.inner.b = 100
    o.c = 9
    return o.inner.a + o.inner.b + o.c
}