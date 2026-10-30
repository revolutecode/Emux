[Main]

var r : i64

[[fastcall]]
func Mix(i64 a, u32 b) i64 {
    return a + b
}

func Start() i64 {
    r = Mix(5000000000, 7)
    return r / 1000000000 + 37
}
