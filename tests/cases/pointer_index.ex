[Main]

var text : B8
var p : u8*

func Start() i32 {
    text = "AB"
    p = text
    p[1] = 67
    return p[1] + text[1] - 67
}
