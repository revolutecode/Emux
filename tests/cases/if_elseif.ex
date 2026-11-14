[Main]

global n : i32
global r : i32

global Start() i32 {
    n = 15
    r = 0
    if (n < 10) {
        r = 1
    } else if (n < 20) {
        r = 2
    } else {
        r = 3
    }
    return r
}
