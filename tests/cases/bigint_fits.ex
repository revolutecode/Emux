[Main]

global value : i64

global Start() i32 {
    value = static_cast<i64>(0x10000000000000000 - 1)
    return static_cast<i32>(value & 0x3F)
}
