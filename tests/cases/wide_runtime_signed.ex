[Main]
var s : i128
var t : i128

func Start() i32 {
    s = -100000000000000000000
    t = 7
    s = s / t
    s = s + 14285714285714285714
    s = s + 42
    return static_cast<i32>(s)
}
