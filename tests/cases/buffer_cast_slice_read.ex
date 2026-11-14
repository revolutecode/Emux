[Main]

[[const]] global word : B8 = "ABCD"

global Start() i32 {
    return static_cast<u16>(word + 2) >> 8
}
