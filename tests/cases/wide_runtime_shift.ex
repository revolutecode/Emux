[Main]
global a : u128
global c : u128

global Start() i32 {
    a = 1
    c = 100
    a = a << c
    c = 98
    a = a >> c
    return static_cast<i32>(a)
}
