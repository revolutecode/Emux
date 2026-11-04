[Main]

var text : B4

func Start() i32 {
    text = "ABC"
    text[0] += 1
    return *text
}
