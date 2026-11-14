[Main]
global a : i32
global b : i32

global Start() i32 {
    a = 17
    b = 5
    return a % b + 30
}
