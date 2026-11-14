[Main]

global text : u8*
global other : u8*

global Start() i32 {
    text = "AB"
    other = Text::Initial()
    return Text::First(text)
}
