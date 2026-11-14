[Main]
global a : u128
global b : u128
global r : u128

global Start() i32 {
    a = 1267650600228229401496703205376
    b = 18446744073709551617
    r = a / b
    r = r & 255
    return static_cast<i32>(r)
}
