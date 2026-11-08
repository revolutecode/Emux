[Main]

[[const]] var word : B8 = "ABCD"

func Start() i32 {
    return static_cast<u32>(word) % 256
}
