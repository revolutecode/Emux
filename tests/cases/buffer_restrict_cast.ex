[Main]

[[restrict]] var text : B4
var p : u8*

func Start() i32 {
    text = "AB"
    p = static_cast<u8*>(&text)
    return *p
}
