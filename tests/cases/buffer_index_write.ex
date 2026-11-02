[Main]

var text : B4

func Start() i32 {
    text = "A"
    text[1] = 66
    return text[1]
}
