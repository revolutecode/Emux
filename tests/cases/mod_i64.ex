[Main]
var a : i64

func Start() i32 {
    a = -17
    a = a % 5
    return static_cast<i32>(a) + 30
}
