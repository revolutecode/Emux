[Main]

var text : B8 = "ABCD"

func Start() i32 {
    return *(text + 8)
}
