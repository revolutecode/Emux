[Main]

var values : B9
var p : u16*

func Start() i32 {
    values = "ABCDEFGH"
    p = values
    p = p + 1
    return *p
}
