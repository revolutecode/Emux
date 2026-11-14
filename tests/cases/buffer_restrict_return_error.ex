[Main]

[[restrict]] global text : B4

global Address() u8* {
    return text
}

global Start() i32 {
    text = "AB"
    return 0
}
