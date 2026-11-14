[Main]

global r : i64

[[fastcall]]
global Mix(i64 a, u32 b) i64 {
    return a + b
}

global Start() i64 {
    r = Mix(5000000000, 7)
    return r / 1000000000 + 37
}
