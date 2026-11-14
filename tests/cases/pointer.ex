[Main]

global value : i32
global slot : i32*

global Store(i32* target, i32 amount) void {
    *target = amount
}

global Start() i32 {
    Store(&value, 40)
    slot = &value
    *slot = *slot + 2
    return value
}
