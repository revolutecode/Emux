[Main]

[[noframe]] func Add(i32 a, i32 b) i32 {
    local r : i32 = a + b
    return r
}

func Start() i32 {
    return Add(2, 3)
}
