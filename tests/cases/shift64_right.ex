[Main]

global a : i64
global n : i64

global Start() i32 {
    a = 4294967296
    n = 32
    a = a >> n
    return a
}
