[Main]

var text : B8

func Get() B8 {
    return text
}

func Start() i32 {
    text = "Q"
    return *Get()
}
