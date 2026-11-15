[Main]

global a : i64
global n : i64

global Start() i32 {
    a = 4294967296
    n = 32
    a = static_cast<i64>(a >> n)
    return a
}
