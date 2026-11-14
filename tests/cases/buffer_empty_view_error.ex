[Main]

global text : B8 = "ABCD"

global Start() i32 {
    return *(text + 8)
}
