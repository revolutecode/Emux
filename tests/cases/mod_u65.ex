[Main]
global w : u65
global r : u65

global Start() i32 {
    w = 18446744073709551623
    r = w % 10
    return static_cast<i32>(r)
}
