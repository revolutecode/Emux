[Main]

global values : B5
global p : u8*

global Start() i32 {
    values = "ABCD"
    p = values
    p += 3
    p -= 2
    return *p
}
