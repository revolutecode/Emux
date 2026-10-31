[Main]

var temp : i32

func Store(i32* target, i32 amount) void {
    *target = amount
}

func Double(i32 value) i32 {
    Store(&value, value + value)
    return value
}

func Start() i32 {
    temp = 21
    return Double(temp)
}
