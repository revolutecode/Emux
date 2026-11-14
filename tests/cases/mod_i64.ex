[Main]
global a : i64

global Start() i32 {
    a = -17
    a = a % 5
    return static_cast<i32>(a) + 30
}
