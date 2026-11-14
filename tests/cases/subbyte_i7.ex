[Main]
global a : i7

global Start() i32 {
    a = 20
    a = a * 10
    return static_cast<i32>(a) + 98
}
