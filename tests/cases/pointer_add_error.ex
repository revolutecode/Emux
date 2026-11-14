[Main]

global values : B5
global p : u8*

global Start() i32 {
    values = "ABCD"
    p = values
    p = p + p
    return 0
}
