[Main]

[[const]] global word : B8 = "ABCD"

global Start() i32 {
    return static_cast<u32>(word) % 256
}
