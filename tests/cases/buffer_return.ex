[Main]

global text : B8

global Get() B8 {
    return text
}

global Start() i32 {
    text = "Q"
    return *Get()
}
