[Main]

[[frame]] [[noframe]] global Stable(i32 a) i32 {
    return a
}

global Start() i32 {
    return Stable(1)
}
