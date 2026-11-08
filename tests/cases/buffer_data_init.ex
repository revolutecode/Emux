[Main]

var text : B8 = "AB"
var copy : B8

func Start() i32 {
    copy = text
    text = "CD"
    return *copy
}
