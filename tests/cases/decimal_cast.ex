[Main]
var x : q8.4

func Start() i32 {
    x = static_cast<q8.4>(1.5)
    return static_cast<i32>(x * 2)
}
