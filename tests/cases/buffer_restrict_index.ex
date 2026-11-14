[Main]

[[restrict]] global text : B4

global Start() i32 {
    text = "AB"
    return text[1] + *text
}
