[Main]
var a : u128
var c : u128

func Start() i32 {
    a = 1
    c = 100
    a = a << c
    c = 98
    a = a >> c
    return static_cast<i32>(a)
}
