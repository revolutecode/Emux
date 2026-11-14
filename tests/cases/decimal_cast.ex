[Main]
global x : q8.4

global Start() i32 {
    x = static_cast<q8.4>(1.5)
    return static_cast<i32>(x * 2)
}
