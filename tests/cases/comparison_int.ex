[Main]

global r : i32

global Start() i32 {
    r = 0
    r = r + (5 == 5)
    r = r + (5 != 6)
    r = r + (5 < 6)
    r = r + (6 > 5)
    r = r + (5 <= 5)
    r = r + (6 >= 5)
    r = r + (5 == 6)
    r = r + (5 < 4)
    return r
}
