[Main]

global Start() i32 {
    local buf : B8 = "abc"
    buf[0] = 66
    return buf[0]
}
