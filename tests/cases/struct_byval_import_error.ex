[Main]

[[packed]]
struct Pair {
    a : u16,
    b : u16
}

[[import("kernel32")]]
global Native(Pair p) i32

global Start() i32 {
    local p : Pair
    return Native(p)
}