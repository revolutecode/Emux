[Main]

global text : B8
global p : u8*

global Start() i32 {
    text = "AB"
    p = text
    p[1] = 67
    return p[1] + text[1] - 67
}
