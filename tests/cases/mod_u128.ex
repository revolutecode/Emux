[Main]
var w : u128
var r : u128

func Start() i32 {
    w = 18446744073709551623
    r = w % 10
    return static_cast<i32>(r) + 10
}
