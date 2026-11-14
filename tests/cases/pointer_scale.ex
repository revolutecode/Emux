[Main]

global values : B9
global p : u16*

global Start() i32 {
    values = "ABCDEFGH"
    p = values
    p = p + 1
    return *p
}
