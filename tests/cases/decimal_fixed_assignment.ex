[Main]
global x : q32.16

global Start() i32 {
    x = 1.5
    return static_cast<i32>(x)
}
