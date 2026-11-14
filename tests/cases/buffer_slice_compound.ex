[Main]

global text : B8 = "ABCD"

global Start() i32 {
    *(text + 1) += 1
    return *text + *(text + 1)
}
