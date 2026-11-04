[Main]

var a : i32
var b : i32
var r : i32

func Start() i32 {
    a = -1
    b = 1
    r = 0
    r = r + (a < b)
    r = r + (a > b)
    return r
}
