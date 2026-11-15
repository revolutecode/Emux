[Main]

[[unrestrict]]
struct A {
    x : u32,
    y : u32
}

struct B {
    x : u32,
    y : u32
}

global Start() i32 {
    local a : A
    local b : B
    b.x = 4
    b.y = 6
    a = b
    return a.x + a.y
}