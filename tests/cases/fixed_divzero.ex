[Main]
global a : q32.16
global b : q32.16
global i : i32

global Start() i32 {
    a = 5
    b = 0
    a = a / b
    i = static_cast<i32>(a)
    return i + 42
}
