[Main]
global a : u3

global Start() i32 {
    a = 7
    a = a % 4
    return static_cast<i32>(a)
}
