[Main]

global Add(i32 a, i32 b) i32 {
    return a + b
}

global Start() i32 {
    local base : i32 = 40
    local r : i32 = Add(1, 1)
    return base + r
}
