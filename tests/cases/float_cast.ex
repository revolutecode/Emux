[Main]
var f : f32
var q : q32.16
var i : i32

func Start() i32 {
    q = 3
    f = static_cast<f32>(q)
    q = static_cast<q32.16>(f)
    i = static_cast<i32>(q)
    return i + 39
}
