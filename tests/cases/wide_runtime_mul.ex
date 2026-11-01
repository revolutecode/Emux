[Main]
var a : u128
var b : u128
var r : u128

func Start() i32 {
    a = 18446744073709551658
    b = 18446744073709551617
    r = a * b
    r = r >> 64
    return static_cast<i32>(r)
}
