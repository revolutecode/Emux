[Main]

global a : i64
global b : i64
global q : i64

global Start() i64 {
    a = 5000000000
    b = 1000000000
    q = (a / b) * 8 + 2
    return q
}
