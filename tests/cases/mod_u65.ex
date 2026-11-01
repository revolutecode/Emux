[Main]
var w : u65
var r : u65

func Start() i32 {
    w = 18446744073709551623
    r = w % 10
    return static_cast<i32>(r)
}
