[Main]

func AddHalf(q8.4 v) q8.4 {
    return v + 0.5
}

func Start() i32 {
    return static_cast<i32>(AddHalf(1.5))
}
