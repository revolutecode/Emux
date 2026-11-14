[Main]
global a : f24
global b : f24

global Start() i32 {
    a = 10
    b = 4
    a = a / b
    b = a * 8
    return static_cast<i32>(b) + 22
}
