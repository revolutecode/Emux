[Main]

var r : i64

func AddShift(i64 a, i64 b) i64 {
    return (a + b) * 2
}

func Start() i64 {
    r = AddShift(5000000000, 1000000000)
    return r / 1000000000 + 30
}
