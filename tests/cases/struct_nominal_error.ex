[Main]

struct A {
    x : u32
}

struct B {
    x : u32
}

global Start() i32 {
    local a : A
    local b : B
    a = b
    return 0
}