[Main]

[[restrict]] var text : B4

func Start() i32 {
    text = "AB"
    return text[1] + *text
}
