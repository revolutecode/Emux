[Main]

global Half(f32 v) f32 {
    return v / 2
}

global Start() i32 {
    return static_cast<i32>(Half(84))
}
