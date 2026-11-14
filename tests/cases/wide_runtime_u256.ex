[Main]
global a : u256
global b : u256
global r : u256

global Start() i32 {
    a = 115792089237316195423570985008687907853269984665640564039457584007913129639935
    b = 18446744073709551617
    r = a / b
    r = r & 255
    return static_cast<i32>(r)
}
