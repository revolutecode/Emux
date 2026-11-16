[Main]

[[global]] local helper() i32 {
    return 42
}

global Start() i32 {
    return helper()
}
