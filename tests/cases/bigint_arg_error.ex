[Main]

global Take(u8 value) u8 {
    return value
}

global Start() i32 {
    return Take(0x10000000000000000)
}
