[Main]

var value : i64

func Start() i32 {
    value = 0x10000000000000000 - 1
    return static_cast<i32>(value & 0x3F)
}
