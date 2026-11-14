[Main]
global a : f128
global b : f128
global i : i32

global Start() i32 {
    a = 10
    b = 4
    i = static_cast<i32>(a / b)
    a = a + b
    b = a * 3
    return i + static_cast<i32>(b) - 2
}
