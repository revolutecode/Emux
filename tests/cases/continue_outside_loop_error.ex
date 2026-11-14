[Main]

global i : i32

global Start() i32 {
    for (i = 0; i < 4; i = i + 1) {
        i = i
    }
    continue
    return i
}
