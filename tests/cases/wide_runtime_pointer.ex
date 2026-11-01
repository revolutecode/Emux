[Main]
var v : u128
var p : u128*

func Start() i32 {
    v = 18446744073709551616
    p = &v
    *p = *p + 5
    return static_cast<i32>(v & 255)
}
