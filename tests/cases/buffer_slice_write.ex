[Main]

var text : B8 = "ABCD"

func Start() i32 {
    *(text + 3) = 90
    return *(text + 3)
}
