[Main]
global a : f32
global i : i32

global Start() i32 {
    a = 10000000000000000000000000000000000000000
    i = static_cast<i32>(a)
    return i + 42
}
