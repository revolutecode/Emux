[Main]

[[fastcall]]
func Add3(u32 a, u32 b, u32 c) u32 {
    return a + b + c
}

[[stdcall]]
func Mul(i32 a, i32 b) i32 {
    return a * b
}

func Start() i32 {
    return Mul(2, 3) + Add3(10, 20, 6)
}
