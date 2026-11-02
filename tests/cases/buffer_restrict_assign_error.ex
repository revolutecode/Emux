[Main]

[[restrict]] var text : B4
var p : u8*

func Start() i32 {
    text = "AB"
    p = text
    return 0
}
