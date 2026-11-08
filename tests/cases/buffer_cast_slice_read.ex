[Main]

[[const]] var word : B8 = "ABCD"

func Start() i32 {
    return static_cast<u16>(word + 2) >> 8
}
