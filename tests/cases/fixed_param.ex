[Main]

global Scale(q32.16 v) q32.16 {
    return v * 2
}

global Start() i32 {
    return static_cast<i32>(Scale(21))
}
