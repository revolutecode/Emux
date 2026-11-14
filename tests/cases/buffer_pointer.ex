[Main]

global text : B4
global cursor : u8*

global Start() i32 {
    text = "A"
    cursor = &text
    return *cursor
}
