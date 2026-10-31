[Main]

var source : B16
var destination : B16

func Start() i32 {
    source = "hello"
    destination = source
    return *destination
}
