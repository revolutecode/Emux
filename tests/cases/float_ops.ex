[Main]
global a : f32
global b : f32
global i : i32

global Start() i32 {
    a = 10
    b = 4
    i = static_cast<i32>(a / b)
    a = a + b
    b = a * 2
    a = b - 20
    b = -b
    i = i + static_cast<i32>(a)
    i = i + static_cast<i32>(b)
    return i + 60
}
