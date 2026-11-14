[Main]
global v : u128
global p : u128*

global Start() i32 {
    v = 18446744073709551616
    p = &v
    *p = *p + 5
    return static_cast<i32>(v & 255)
}
