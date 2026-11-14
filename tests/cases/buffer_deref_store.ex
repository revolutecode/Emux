[Main]

global text : B4

global Start() i32 {
    text = "A"
    *text = 66
    return *text
}
