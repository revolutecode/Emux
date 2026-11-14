[Main]
global s : i129

global Start() i32 {
    s = 340282366920938463463374607431768211455
    s = s + s
    s = s >> 128
    return static_cast<i32>(s & 255)
}
