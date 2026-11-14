[Main]

global text : B8
global p : u8*

global Start() i32 {
    text = "AB"
    p = text
    return *p
}
