[Main]

var text : B8
var p : u8*

func Start() i32 {
    text = "AB"
    p = text
    return *p
}
