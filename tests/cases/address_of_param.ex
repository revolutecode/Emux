[Main]

global temp : i32

global Store(i32* target, i32 amount) void {
    *target = amount
}

global Double(i32 value) i32 {
    Store(&value, value + value)
    return value
}

global Start() i32 {
    temp = 21
    return Double(temp)
}
