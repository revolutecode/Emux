[Main]
global a : q40.8
global b : q40.8
global i : i32

global Start() i32 {
    a = 300
    b = 7
    i = static_cast<i32>(a / b)
    a = a * b
    b = 9
    b = a % b
    a = a - b
    i = i + static_cast<i32>(a) - 2097
    return i
}
