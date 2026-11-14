[Main]

global First(u8* data) u8 {
    return *data
}

global Start() i32 {
    return First("\x2A")
}
