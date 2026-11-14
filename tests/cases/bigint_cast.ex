[Main]

global Start() i32 {
    return static_cast<i32>(0x123456789ABCDEF0123 & 0xFF)
}
