[Main]
global x : q8.4

global Start() i32 {
    x = 1.5 + 0.5
    return static_cast<i32>(x)
}
