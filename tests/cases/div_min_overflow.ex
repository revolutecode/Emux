[Main]
global a : i64
global b : i64

global Start() i32 {
    a = -9223372036854775807 - 1
    b = -1
    a = a / b
    return static_cast<i32>(a)
}