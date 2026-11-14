[Main]
global a : u72
global b : u72
global r : u72

global Start() i32 {
    a = 2361183241434822606901
    b = 68719476739
    r = a * b
    r = r + a
    return static_cast<i32>(r & 255)
}
