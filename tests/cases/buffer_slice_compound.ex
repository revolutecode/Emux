[Main]

var text : B8 = "ABCD"

func Start() i32 {
    *(text + 1) += 1
    return *text + *(text + 1)
}
