[Main]

var values : B5
var p : u8*
var q : u8*

func Start() i32 {
    values = "ABCD"
    p = values
    q = p + 3
    return q - p
}
