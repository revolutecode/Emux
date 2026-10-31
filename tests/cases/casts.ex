[Main]

var value : i32
var address : uptr

func Start() i32 {
    value = 42
    address = reinterpret_cast<uptr>(&value)
    return static_cast<i32>(value)
}
