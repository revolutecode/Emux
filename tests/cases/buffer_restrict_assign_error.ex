[Main]

[[restrict]] global text : B4
global p : u8*

global Start() i32 {
    text = "AB"
    p = text
    return 0
}
