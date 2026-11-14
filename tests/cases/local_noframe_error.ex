[Main]

[[noframe]] global Add(i32 a, i32 b) i32 {
    local r : i32 = a + b
    return r
}

global Start() i32 {
    return Add(2, 3)
}
