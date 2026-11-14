[Main]

global values : B5
global p : u8*
global q : u8*

global Start() i32 {
    values = "ABCD"
    p = values
    q = p + 3
    return q - p
}
