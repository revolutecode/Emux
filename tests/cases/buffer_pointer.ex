[Main]

var text : B4
var cursor : u8*

func Start() i32 {
    text = "A"
    cursor = &text
    return *cursor
}
