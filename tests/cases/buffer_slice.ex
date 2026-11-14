[Main]

[[const]] global text : B13 = "Hello, World"

global Start() i32 {
    return *(text + 7)
}
