[Main]

func First(u8* data) u8 {
    return *data
}

func Start() i32 {
    return First("\x2A")
}
