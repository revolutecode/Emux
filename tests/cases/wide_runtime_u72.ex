[Main]
var a : u72
var b : u72
var r : u72

func Start() i32 {
    a = 2361183241434822606901
    b = 68719476739
    r = a * b
    r = r + a
    return static_cast<i32>(r & 255)
}
