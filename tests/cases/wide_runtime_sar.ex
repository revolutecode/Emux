[Main]
global s : i128

global Start() i32 {
    s = 170141183460469231731687303715884105727
    s = s + 1
    s = s >> 127
    s = s & 255
    return static_cast<i32>(s)
}
