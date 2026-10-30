[Main]

[[export("dup")]]
func A() i32 {
    return 1
}

[[export("dup")]]
func B() i32 {
    return 2
}

func Start() i32 {
    return A() + B()
}
