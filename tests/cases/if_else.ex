[Main]

global a : i32
global r : i32

global Start() i32 {
    a = 5
    r = 0
    if (a > 3) {
        r = 10
    } else {
        r = 20
    }
    return r
}
