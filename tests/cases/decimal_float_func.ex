[Main]

func Twice(f32 v) f32 {
    return v * 2.0
}

func Start() i32 {
    return static_cast<i32>(Twice(1.75))
}
