[Main]

var value : i32
var slot : i32*

func Store(i32* target, i32 amount) void {
    *target = amount
}

func Start() i32 {
    Store(&value, 40)
    slot = &value
    *slot = *slot + 2
    return value
}
