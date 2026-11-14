[Main]
global a : Q4.2
global b : q32.16
global wide : Q32.16
global i : i32

global Start() i32 {
    a = 7
    b = 2
    i = static_cast<i32>(a / b)
    a = a * b
    b = 4
    b = a % b
    a = a - 4
    b = -b
    i = i + static_cast<i32>(a)
    i = i + static_cast<i32>(b)
    wide = 5
    return i + static_cast<i32>(a + b) + static_cast<i32>(wide) + 18
}
