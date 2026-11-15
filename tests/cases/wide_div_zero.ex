[Main]
global a : u128
global b : u128
global r : u128

global Start() i32 {
    a = 100
    b = 0
    r = a / b
    return static_cast<i32>(r)
}