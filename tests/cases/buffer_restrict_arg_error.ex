[Main]

[[restrict]] var text : B4

func First(u8* data) u8 {
    return *data
}

func Start() i32 {
    text = "AB"
    return First(text)
}
