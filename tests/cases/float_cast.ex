[Main]
global f : f32
global q : q32.16
global i : i32

global Start() i32 {
    q = 3
    f = static_cast<f32>(q)
    q = static_cast<q32.16>(f)
    i = static_cast<i32>(q)
    return i + 39
}
