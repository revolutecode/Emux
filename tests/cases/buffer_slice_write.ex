[Main]

global text : B8 = "ABCD"

global Start() i32 {
    *(text + 3) = 90
    return *(text + 3)
}
