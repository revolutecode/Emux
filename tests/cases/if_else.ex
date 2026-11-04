[Main]

var a : i32
var r : i32

func Start() i32 {
    a = 5
    r = 0
    if (a > 3) {
        r = 10
    } else {
        r = 20
    }
    return r
}
