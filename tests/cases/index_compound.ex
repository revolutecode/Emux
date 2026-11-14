[Main]

global text : B4

global Start() i32 {
    text = "ABC"
    text[0] += 1
    return *text
}
