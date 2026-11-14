[Main]

global Fact(i32 n) i32 {
    local r : i32 = 1
    if (n > 1) {
        local sub : i32 = Fact(n - 1)
        r = sub * n
    }
    return r
}

global Start() i32 {
    return Fact(5) % 256
}
