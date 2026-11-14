[Main]

global src : B4 = "ab"

global Start() i32 {
    local dst : B8 = src
    dst[3] = 33
    return dst[1] + dst[3] - 99
}
