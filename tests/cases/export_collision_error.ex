[Main]

[[export("dup")]]
global A() i32 {
    return 1
}

[[export("dup")]]
global B() i32 {
    return 2
}

global Start() i32 {
    return A() + B()
}
