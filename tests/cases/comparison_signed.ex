[Main]

global a : i32
global b : i32
global r : i32

global Start() i32 {
    a = -1
    b = 1
    r = 0
    r = r + (a < b)
    r = r + (a > b)
    return r
}
