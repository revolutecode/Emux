[Main]

var values : B5
var p : u8*

func Start() i32 {
    values = "ABCD"
    p = values
    p *= 2
    return 0
}
