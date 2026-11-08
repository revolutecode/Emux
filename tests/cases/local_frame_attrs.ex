[Main]

[[frame]] func Stable(i32 a, i32 b) i32 {
    local r : i32 = a * b
    local p : i32* = &r
    return *p
}

[[noframe]] func Sum(i32 a, i32 b) i32 {
    return a + b
}

func Start() i32 {
    local base : i32 = Stable(6, 7)
    return base + Sum(base, 0) - 42
}
