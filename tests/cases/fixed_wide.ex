[Main]
global a : q128.16
global b : q128.16
global i : i32

global Start() i32 {
    a = 10
    b = 4
    i = static_cast<i32>(a / b)
    a = a * b
    b = a + b
    return i + static_cast<i32>(b) - 4
}
