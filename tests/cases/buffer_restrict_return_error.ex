[Main]

[[restrict]] var text : B4

func Address() u8* {
    return text
}

func Start() i32 {
    text = "AB"
    return 0
}
