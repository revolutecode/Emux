[Main]

global text : B4

global First(u8* data) u8 {
    return *data
}

global Start() i32 {
    text = "Z"
    return First(text)
}
