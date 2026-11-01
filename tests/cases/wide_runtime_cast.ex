[Main]
var w : i128

func Start() i32 {
    w = static_cast<i128>(static_cast<i8>(-5))
    w = w + 10
    return static_cast<i32>(w)
}
