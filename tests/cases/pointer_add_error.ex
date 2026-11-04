[Main]

var values : B5
var p : u8*

func Start() i32 {
    values = "ABCD"
    p = values
    p = p + p
    return 0
}
