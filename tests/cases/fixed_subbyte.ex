[Main]
global a : q24.8
global b : q24.8
global i : i32

global Start() i32 {
    a = 7
    b = 2
    i = static_cast<i32>(a / b)
    a = a * b
    b = 4
    b = a % b
    a = a - 4
    i = i + static_cast<i32>(a)
    i = i + static_cast<i32>(b)
    return i + static_cast<i32>(a - b) + 19
}
